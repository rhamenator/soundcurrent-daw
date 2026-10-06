#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in duration qualifier with owned routes and retained artifacts.

Uses the existing PipeWire daemon without changing rate/quantum/defaults/hardware.
Deadline correctness and sample correctness remain separate report fields.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import platform
import subprocess
import tempfile
import time
from verify_pipewire_fixture import ROOT, snapshot, unchanged, thread_schedulers


def node_inventory():
    data = json.loads(subprocess.run(['pw-dump'], capture_output=True, text=True,
                                    check=True, timeout=5).stdout)
    keys = ['node.name', 'node.description', 'media.class', 'node.driver', 'priority.driver',
            'factory.name', 'object.serial', 'node.rate', 'node.latency', 'node.group',
            'clock.quantum-limit', 'device.description', 'audio.position']
    return {str(o['id']): {k: o.get('info', {}).get('props', {}).get(k) for k in keys}
            for o in data if o['type'] == 'PipeWire:Interface:Node'}


def run(binary, seconds, mode, diagnostics):
    before = snapshot()
    assert not before['owned'], 'An owned fixture is already running'
    owned_root = Path(tempfile.mkdtemp(prefix='sc-native-duration-', dir=ROOT / '.cache'))
    project = owned_root / 'Durée native — Ελληνικά'
    stdout_path, stderr_path = owned_root / 'stdout.log', owned_root / 'stderr.log'
    inventory = node_inventory()
    diagnostics.update(project_directory=str(project), stdout_file=str(stdout_path),
                       stderr_file=str(stderr_path), scheduler_samples=[], observations=[])
    started = time.monotonic()
    with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
        child = subprocess.Popen([binary, project, str(seconds), mode], stdout=stdout, stderr=stderr)
        diagnostics['owned_pid'] = child.pid
        try:
            deadline = started + seconds + 360
            observations, linked, next_report = 0, 0, started + 30
            while child.poll() is None:
                assert time.monotonic() < deadline, 'Owned native duration child timed out'
                current = snapshot()
                unchanged(before, current)
                observations += 1
                linked += bool(current['edges'])
                if current['edges'] and not diagnostics['scheduler_samples']:
                    diagnostics['scheduler_samples'] = thread_schedulers(child.pid)
                    diagnostics['active_native_nodes'] = node_inventory()
                if time.monotonic() >= next_report:
                    rows = []
                    for journal in project.glob('media/capture-*/journal.json'):
                        try:
                            value = json.loads(journal.read_text())
                            if value.get('timingOrigin', {}).get('backend') == 2:
                                rows.append(value['committedFrames'])
                        except (OSError, ValueError, AttributeError):
                            continue  # Atomic journal replacement/activation may race this observation.
                    sample = {'wall_seconds': time.monotonic() - started,
                              'native_journals': len(rows), 'minimum_durable_frames': min(rows) if rows else 0,
                              'maximum_durable_frames': max(rows) if rows else 0,
                              'loadavg': Path('/proc/loadavg').read_text().strip()}
                    diagnostics['observations'].append(sample)
                    print(json.dumps({'progress': sample}), flush=True)
                    next_report += 30
                time.sleep(1)
            child.wait(timeout=5)
            diagnostics.update(exit_code=child.returncode, wall_seconds=time.monotonic() - started)
            assert child.returncode == 0, f'Native duration failed; retained {stderr_path}'
        finally:
            if child.poll() is None:
                child.kill()  # Only this newly spawned child; never restart on observation timeout.
                child.wait(timeout=10)
                diagnostics.update(exit_code=child.returncode, killed_owned_child=True)
            until = time.monotonic() + 5
            while True:
                after = snapshot()
                unchanged(before, after)
                if not after['owned'] or time.monotonic() >= until:
                    break
                time.sleep(.1)
            diagnostics['cleanup_observation'] = {'defaults_unchanged': True,
                'pre_existing_links_preserved': len(before['links']),
                'owned_nodes_remaining': len(after['owned']), 'owned_links_remaining': len(after['edges'])}
            assert not after['owned'] and not after['edges'], 'Native duration nodes/links leaked'
    result = json.loads(stdout_path.read_text())
    assert result['mode'] == mode and result['owned_nodes_only'] and linked > 0
    if mode == 'normal':
        frames = seconds * 48000
        assert result['frames_per_raw_take'] == result['sink_frames'] == frames
        assert result['verified_raw_samples'] == 32 * frames and result['verified_output_samples'] == 2 * frames
        assert result['maximum_sample_difference'] == result['missing_track_frames'] == 0
        assert result['rt_allocations'] == result['rt_frees'] == result['rt_blocking_locks'] == 0
        assert result['save_reopen'] and result['maximum_output_peak'] > 1
        assert result['stream_wall_seconds'] >= seconds - .1, 'Native range ran faster than declared duration'
        assert all(t['samples'] == t['calls'] and t['complete_timing_coverage'] for t in result['callback_timing'].values())
        result['observed_native_clock_node'] = diagnostics.get('active_native_nodes', inventory).get(str(result['clock_id']))
        result['finite_deadline_thresholds_met'] = all(t['finite_deadline_thresholds_met'] for t in result['callback_timing'].values())
        result['declared_30_minute_native_sample_qualified'] = seconds >= 1800
        result['declared_30_minute_fixed_workload_deadline_qualified'] = seconds >= 1800 and result['finite_deadline_thresholds_met']
    elif mode == 'writer-stall':
        assert result['capture_failed_retained'] and result['initiating_lane'] == 17
        assert result['injected_journal_stall_ms'] == 4000 and result['canonical_unchanged']
        assert result['verified_raw_samples'] > 0 and result['verified_output_samples'] > 0
        assert result['maximum_sample_difference'] == 0
        assert result['rt_allocations'] == result['rt_frees'] == result['rt_blocking_locks'] == 0
        assert result['minimum_raw_frames'] <= result['maximum_raw_frames'] < seconds * 48000
        timing = result['disk_timing'][17]
        assert timing['phase_pairs_complete'] and timing['maximum_ready_slabs'] == 32
        journal = next(p for p in timing['phase_summaries'] if p['phase'] == 'journal_publish')
        assert journal['maximum_ns'] >= 4_000_000_000 and journal['end_ready_slabs'] == 32
        assert journal['end_committed_frames'] >= journal['begin_written_frames']
    else:
        assert result['device_lost_retained'] and result['verified_raw_samples'] > 0
    result['scheduler_samples'] = diagnostics['scheduler_samples']
    result['routing_observations'] = {'graph_snapshots': observations, 'linked_snapshots': linked,
                                    **diagnostics['cleanup_observation']}
    result['host'] = {'platform': platform.platform(), 'logical_processors': os.cpu_count(),
                      'controlled_competing_load': False,
                      'fixed_workload': '32 mono raw/Post-EQ arms with non-flat default bands, one unarmed file, signed stereo matrix, disk sink',
                      'physical_roundtrip_qualified': False, 'windows_qualified': False}
    result['project_directory'] = str(project)
    result['supervisor_wall_seconds'] = diagnostics['wall_seconds']
    result['progress_observations'] = diagnostics['observations']
    result['artifacts'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in [stdout_path, stderr_path]}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--modes', nargs='+', choices=['normal', 'sink-gap', 'writer-stall'], default=['normal'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--failure-output', type=Path, required=True)
    args = parser.parse_args()
    assert 1 <= args.seconds <= 1800
    assert 'writer-stall' not in args.modes or args.seconds >= 8, 'Writer stall needs at least eight seconds'
    results = []
    diagnostics = {}
    try:
        for mode in args.modes:
            diagnostics = {}
            results.append(run(args.binary.resolve(), args.seconds, mode, diagnostics))
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        args.failure_output.write_text(json.dumps({'error': str(error), 'diagnostics': diagnostics,
                                                 'completed_cases': results}, indent=2, ensure_ascii=False) + '\n')
        raise
    args.output.write_text(json.dumps(results, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({'passed_modes': [r['mode'] for r in results], 'output': str(args.output)}), flush=True)


if __name__ == '__main__':
    main()
