#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify punch fault recovery on synthetic or owned native routes; retain all artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from verify_pipewire_fixture import snapshot, unchanged, thread_schedulers

ROOT = Path(__file__).resolve().parents[1]


def qualify(result, native, mode):
    assert result['native'] == native and result['mode'] == mode
    assert result['armed_tracks'] == 32
    assert result['punch_in'] == 48150 and result['punch_out'] == 144164
    assert result['playback_start'] == 137
    assert 48150 < result['stopped_position'] < 144164
    assert result['status'] == {'cancel': 4, 'source-loss': 9, 'sink-loss': 9, 'writer-fail': 12}[mode]
    assert result['original_errors'] == {'cancel': 32, 'source-loss': 0, 'sink-loss': 0, 'writer-fail': 1}[mode]
    assert result['original_errors'] + result['original_takes'] == result['recovered_takes'] == 32
    assert result['initiating_writer_error_retained'] == (mode == 'writer-fail')
    assert result['maximum_sample_difference'] == result['missing_track_frames'] == 0
    assert result['rt_allocations'] == result['rt_frees'] == result['rt_blocking_locks'] == 0
    assert result['save_reopen'] and result['grouped_undo_redo']
    assert result['original_media_unchanged'] and result['original_journals_unchanged']
    assert not result['physical_latency_qualified'] and not result['windows_qualified']
    assert not result['sustained_performance_qualified']
    rows = result['raw_lanes']
    assert len(rows) == 32 and len({r['track'] for r in rows}) == 32
    assert len({r['recovered_id'] for r in rows}) == len({r['source_id'] for r in rows}) == 32
    assert not ({r['recovered_id'] for r in rows} & {r['source_id'] for r in rows})
    for n, row in enumerate(rows):
        delay = [4097, 0, 41, 200][n % 4]
        assert row['latency'] == delay and row['raw_start'] == 48150 + delay
        assert 0 < row['durable'] <= row['written'] <= row['captured'] < 96014
        assert row['rejected'] == 0 or (mode == 'writer-fail' and n == 17)
        assert row['device_origin'] == result['device_playback_origin'] + 48013 + delay
        assert 0 <= row['origin_offset'] < row['origin_quantum'] <= 2048
        assert row['original_receipt_error'] == (mode == 'cancel' or (mode == 'writer-fail' and n == 17))
        assert len(row['sha256']) == 64
    total = sum(r['durable'] for r in rows)
    assert result['raw_samples_verified'] == result['recovered_samples_verified'] == total > 0
    common = result['output_common_frames']
    assert common == min(result['output_retained_frames'], result['stopped_position'] - 137) > 48013
    assert result['output_samples_verified'] == 2 * common and result['output_peak'] > 1
    assert any(r['origin_offset'] > 0 for r in rows)
    for timing in result['callback_timing'].values():
        assert timing['samples'] == timing['calls'] > 0
        assert timing['complete_timing_coverage'] and timing['complete_cpu_coverage']
        assert timing['complete_thread_usage_coverage']
        if native:
            assert timing['finite_deadline_thresholds_met'], 'Punch fault finite callback gate failed'
            assert timing['cycle_context_samples'] == timing['calls'] and timing['cycle_context_unknown'] == 0
            assert timing['callback_end_after_cycle_period_count'] == 0, 'Punch fault native cycle gate failed'
    if native:
        assert result['owned_nodes_only']


def run(binary, native, mode, output, failure):
    folder = Path(tempfile.mkdtemp(prefix='sc-punch-fault-native-' if native else 'sc-punch-fault-oracle-',
                                   dir=ROOT / '.cache'))
    project = folder / 'Punch — Ελληνικά'
    stdout, stderr = folder / 'stdout.json', folder / 'stderr.log'
    diagnostic = {'project_directory': str(project), 'stdout': str(stdout), 'stderr': str(stderr),
                  'native': native, 'mode': mode, 'scheduler_samples': [], 'graph_snapshots': 0, 'linked_snapshots': 0}
    before = snapshot() if native else None
    if native:
        assert not before['owned'], 'Another owned fixture is active'
    child = None
    started = time.monotonic()
    try:
        with stdout.open('w') as out, stderr.open('w') as err:
            child = subprocess.Popen([binary, project, 'native' if native else 'synthetic', mode], stdout=out, stderr=err)
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
        qualify(result, native, mode)
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
    print(json.dumps({'qualified': ('native punch fault ' if native else 'synthetic punch fault ') + mode,
                      'output': str(output), 'project_directory': str(project),
                      'wall_seconds': diagnostic['wall_seconds']}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--synthetic', action='store_true')
    parser.add_argument('--mode', choices=['cancel', 'source-loss', 'sink-loss', 'writer-fail'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--failure-output', type=Path, required=True)
    args = parser.parse_args()
    run(args.binary.resolve(), not args.synthetic, args.mode, args.output, args.failure_output)


if __name__ == '__main__':
    main()
