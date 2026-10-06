#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in native desktop punch qualification; retain projects, clocks and route evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from verify_pipewire_fixture import snapshot, unchanged, thread_schedulers

ROOT = Path(__file__).resolve().parents[1]


def qualify(result, tracks):
    assert result['mode'] == str(tracks) and result['armed_tracks'] == tracks
    assert result['production_gui_duplex'] and result['canonical_punch_settings']
    assert result['punch_in'] == 48150 and result['punch_out'] == 144164
    assert result['playback_start'] == 137 and result['playback_end'] == 240137
    assert result['frames_per_raw_take'] == 96014
    assert result['raw_samples_verified'] == tracks * 96014
    assert result['output_samples_verified'] == 480000 and result['raw_samples_exact']
    assert result['output_peak'] > 1 and 0 <= result['output_max_difference'] <= 1e-7
    assert result['missing_track_frames'] == 0
    assert result['callback_allocations'] == result['callback_frees'] == result['callback_locks'] == 0
    assert result['live_EQ_receipts'] and result['save_reopen'] and result['group_undo_redo']
    assert result['underlying_clip_unchanged'] and result['owned_nodes_only']
    assert not result['physical_latency_qualified'] and not result['windows_qualified']
    assert not result['sustained_performance_qualified']
    rows = result['raw_lanes']
    assert len(rows) == tracks and len({r['track'] for r in rows}) == tracks
    for row in rows:
        assert row['latency'] == 0 and row['frames'] == 96014 and row['raw_start'] == 48150
        assert row['device_origin'] == result['device_playback_origin'] + 48013
        assert 0 < row['origin_offset'] < row['origin_quantum'] <= 2048
        assert len(row['sha256']) == 64
    for timing in result['callback_timing'].values():
        assert timing['samples'] == timing['calls'] > 0
        assert timing['complete_timing_coverage'] and timing['complete_cpu_coverage']
        assert timing['complete_thread_usage_coverage']
        assert timing['finite_deadline_thresholds_met'], 'Native desktop finite callback timing gate failed'
        assert timing['cycle_context_samples'] == timing['calls'] and timing['cycle_context_unknown'] == 0
        assert timing['callback_end_after_cycle_period_count'] == 0, 'Native desktop current-cycle gate failed'


def run(binary, tracks, output, failure):
    native = True
    folder = Path(tempfile.mkdtemp(prefix='sc-punch-ui-',
                                   dir=ROOT / '.cache'))
    project = folder / 'Punch — Ελληνικά'
    stdout, stderr = folder / 'stdout.json', folder / 'stderr.log'
    diagnostic = {'project_directory': str(project), 'stdout': str(stdout), 'stderr': str(stderr),
                  'native': native, 'scheduler_samples': [], 'graph_snapshots': 0, 'linked_snapshots': 0}
    before = snapshot() if native else None
    if native:
        assert not before['owned'], 'Another owned fixture is active'
    child = None
    started = time.monotonic()
    try:
        with stdout.open('w') as out, stderr.open('w') as err:
            child = subprocess.Popen([binary, project, str(tracks)], stdout=out, stderr=err)
            diagnostic['owned_pid'] = child.pid
            deadline = started + 60
            while child.poll() is None:
                assert time.monotonic() < deadline, 'Owned punch child exceeded observation deadline'
                if native:
                    current = snapshot()
                    unchanged(before, current)
                    diagnostic['graph_snapshots'] += 1
                    diagnostic['linked_snapshots'] += bool(current['edges'])
                    if current['edges'] and not diagnostic['scheduler_samples']:
                        diagnostic['scheduler_samples'] = thread_schedulers(child.pid)
                time.sleep(.2)
            child.wait(timeout=5)
            diagnostic['exit_code'] = child.returncode
        assert child.returncode == 0, f'Punch child failed; retained {stderr}'
        result = json.loads(stdout.read_text())
        diagnostic['child_result'] = result
        qualify(result, tracks)
        if native:
            assert diagnostic['linked_snapshots'] > 0, 'Owned routes never observed'
        diagnostic['wall_seconds'] = time.monotonic() - started
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        diagnostic['error'] = str(error)
    finally:
        if child is not None and child.poll() is None:
            child.kill()  # Only the owned child; terminal wait precedes cleanup or any retry.
            child.wait(timeout=10)
            diagnostic.update(exit_code=child.returncode, killed_owned_child=True)
        if native:
            try:
                deadline = time.monotonic() + 5
                while True:
                    after = snapshot()
                    unchanged(before, after)
                    if not after['owned'] or time.monotonic() >= deadline:
                        break
                    time.sleep(.1)
                diagnostic['routing_observations'] = {
                    'default_metadata_unchanged': True,
                    'pre_existing_links_preserved': len(before['links']),
                    'owned_nodes_remaining': len(after['owned']), 'owned_links_remaining': len(after['edges']),
                    'no_observed_owned_to_unowned_links': True}
                assert not after['owned'] and not after['edges'], 'Owned nodes/links leaked'
            except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
                diagnostic['error'] = diagnostic.get('error', '') + '\nCleanup: ' + str(error)
        diagnostic['artifacts'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in [stdout, stderr] if p.exists()}
        target = failure if 'error' in diagnostic else output
        target.write_text(json.dumps(diagnostic, indent=2, ensure_ascii=False) + '\n')
    if 'error' in diagnostic:
        raise RuntimeError(diagnostic['error'])
    print(json.dumps({'qualified': 'native desktop punch',
                      'output': str(output), 'project_directory': str(project),
                      'wall_seconds': diagnostic['wall_seconds']}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--tracks', type=int, choices=[3, 32], default=32)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--failure-output', type=Path, required=True)
    args = parser.parse_args()
    run(args.binary.resolve(), args.tracks, args.output, args.failure_output)


if __name__ == '__main__':
    main()
