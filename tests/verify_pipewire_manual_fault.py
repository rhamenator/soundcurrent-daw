#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify native manual fault recovery with nonflat output oracle and owned routes."""
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
    cancel = mode in ['cancel', 'early-cancel', 'unserviced-cancel']
    early = mode in ['early-stop', 'early-cancel']
    disk = mode in ['writer-fail', 'hash-fail']
    assert result['native'] == native and result['mode'] == mode
    assert result['armed_tracks'] == 32 and result['reliable_replies'] == 2
    assert result['punch_in'] == 48150 and result['playback_start'] == 137
    assert result['scheduled_out'] == (72163 if mode == 'hash-fail' else 144164)
    assert 48150 < result['stopped_position'] < (480137 if mode == 'hash-fail' else result['scheduled_out'])
    assert result['status'] == (12 if disk else 9 if mode in ['source-loss', 'sink-loss'] else 4)
    assert result['initiating_writer_error_retained'] == disk
    assert result['hash_failure_after_postroll'] == (mode == 'hash-fail')
    assert result['failed_lanes'] == int(disk)
    assert result['canceled_lanes'] == (32 if cancel else 0)
    assert result['empty_lanes'] == (8 if mode == 'early-stop' else 0)
    assert sum(result[k] for k in ['successful_lanes', 'failed_lanes', 'empty_lanes', 'canceled_lanes']) == 32
    if mode == 'unserviced-cancel':
        assert result['first_service_frame'] == -1
    else:
        assert result['first_service_frame'] >= 137
    assert result['explicit_partial_adoption'] == (not cancel)
    assert result['canceled_group_adoption_refused'] == cancel
    rows = result['raw_lanes']
    assert len(rows) == 32 and len({r['track'] for r in rows}) == len({r['source_id'] for r in rows}) == 32
    recovered = [r for r in rows if r['recovered_id'] is not None]
    assert len(recovered) == result['recovered_takes']
    assert recovered or mode == 'early-cancel'
    assert len({r['recovered_id'] for r in recovered}) == len(recovered)
    assert not ({r['source_id'] for r in rows} & {r['recovered_id'] for r in recovered})
    for n, row in enumerate(rows):
        delay = [4097, 0, 41, 200][n % 4]
        assert row['lane'] == n and row['latency'] == delay and row['raw_start'] == 48150 + delay
        assert 0 <= row['durable'] <= row['written'] <= row['captured']
        assert not row['verification_error']
        if not row['captured']:
            assert early and delay == 4097 and not row['recovered_id']
            assert row['outcome'] == (3 if cancel else 1)
        else:
            assert row['device_origin'] == result['device_playback_origin'] + 48013 + delay
            assert 0 <= row['origin_offset'] < row['origin_quantum'] <= 2048
            assert row['outcome'] == (3 if cancel else 2 if disk and n == 17 else 0)
            if disk and n == 17:
                assert row['error'] == ('manual-lane17-finalized-hash' if mode == 'hash-fail' else 'manual-lane17-write')
                assert row['finalized'] == (mode == 'hash-fail')
            elif not cancel:
                assert not row['error'] and row['finalized'] and row['durable'] == row['captured']
            if row['durable']:
                assert len(row['sha256']) == len(row['original_media_sha256']) == len(row['original_journal_sha256']) == 64
    assert result['raw_samples_verified'] == result['recovered_samples_verified'] == sum(r['durable'] for r in rows)
    assert result['raw_samples_verified'] > 0 or mode == 'early-cancel'
    assert any(r.get('origin_offset', 0) > 0 for r in rows)
    common = result['output_common_frames']
    assert common == min(result['output_retained_frames'], result['stopped_position'] - 137) > 48013
    assert result['output_samples_verified'] == 2 * common and result['output_peak'] > 1
    assert 0 <= result['maximum_sample_difference'] <= 1e-6
    assert result['missing_track_frames'] == 0
    assert result['rt_allocations'] == result['rt_frees'] == result['rt_blocking_locks'] == 0
    for key in ['save_reopen', 'original_media_unchanged', 'original_journals_unchanged']:
        assert result[key]
    assert result['grouped_undo_redo'] == bool(recovered)
    for key in ['physical_latency_qualified', 'windows_qualified', 'sustained_performance_qualified']:
        assert not result[key]
    assert set(result['callback_timing']) == {'owner', 'source', 'sink'}
    for timing in result['callback_timing'].values():
        assert timing['samples'] == timing['calls'] > 0
        assert timing['complete_timing_coverage'] and timing['complete_cpu_coverage']
        assert timing['complete_thread_usage_coverage']
        if native:
            assert timing['finite_deadline_thresholds_met'], 'Native manual fault finite timing gate failed'
            assert timing['cycle_context_samples'] == timing['calls'] and timing['cycle_context_unknown'] == 0
            assert timing['callback_end_after_cycle_period_count'] == 0, 'Native manual fault current-cycle gate failed'
    assert result['owned_nodes_only'] == native


def owned_route_details():
    # Diagnostic snapshots only: no foreign node/port metadata retained.
    objects = json.loads(subprocess.run(['pw-dump'], check=True, capture_output=True,
                                       text=True, timeout=5).stdout)
    owned = {o['id'] for o in objects if o['type'] == 'PipeWire:Interface:Node' and
             o.get('info', {}).get('props', {}).get('node.name', '').startswith(
                 ('sc-daw-fixture-', 'sc-daw-playback-', 'sc-daw-recording-'))}
    result = []
    for o in objects:
        props = o.get('info', {}).get('props', {})
        if o['id'] in owned or (o['type'] == 'PipeWire:Interface:Port' and props.get('node.id') in owned) or (
                o['type'] == 'PipeWire:Interface:Link' and
                props.get('link.input.node') in owned and props.get('link.output.node') in owned):
            result.append(o)
    return result


def run(binary, native, mode, output, failure):
    (ROOT / '.cache').mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='sc-manual-fault-native-' if native else 'sc-manual-fault-oracle-',
                                   dir=ROOT / '.cache'))
    project = folder / 'Manual fault — Ελληνικά'
    stdout, stderr = folder / 'stdout.json', folder / 'stderr.log'
    diagnostic = {'project_directory': str(project), 'stdout': str(stdout), 'stderr': str(stderr),
                  'native': native, 'mode': mode,
                  'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                  'verifier_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  'scheduler_samples': [], 'graph_snapshots': 0, 'linked_snapshots': 0}
    before = snapshot() if native else None
    if native:
        assert not before['owned'], 'Another owned fixture is active'
    child = None
    route_details = []
    started = time.monotonic()
    try:
        with stdout.open('w') as out, stderr.open('w') as err:
            child = subprocess.Popen([binary, project, 'native' if native else 'synthetic', mode], stdout=out, stderr=err)
            diagnostic['owned_pid'] = child.pid
            deadline = started + (60 if native else 95)
            while child.poll() is None:
                assert time.monotonic() < deadline, 'Owned manual child exceeded observation deadline'
                if native:
                    current = snapshot()
                    unchanged(before, current)
                    diagnostic['graph_snapshots'] += 1
                    diagnostic['linked_snapshots'] += bool(current['edges'])
                    if current['edges'] and len(route_details) < 32:
                        route_details.append(owned_route_details())
                    if current['edges'] and not diagnostic['scheduler_samples']:
                        diagnostic['scheduler_samples'] = thread_schedulers(child.pid)
                time.sleep(.2)
            child.wait(timeout=5)
            diagnostic['exit_code'] = child.returncode
        assert child.returncode == 0, f'Manual child failed; retained {stderr}'
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
        if route_details:
            routes = folder / 'owned-route-details.json'
            routes.write_text(json.dumps(route_details, indent=2) + '\n')
            diagnostic['owned_route_details'] = {'path': str(routes), 'snapshots': len(route_details), 'sha256': hashlib.sha256(routes.read_bytes()).hexdigest()}
        diagnostic['artifacts'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in [stdout, stderr] if p.exists()}
        target = failure if 'error' in diagnostic else output
        target.write_text(json.dumps(diagnostic, indent=2, ensure_ascii=False) + '\n')
    if 'error' in diagnostic:
        raise RuntimeError(diagnostic['error'])
    print(json.dumps({'qualified': 'native manual' if native else 'synthetic manual oracle',
                      'output': str(output), 'project_directory': str(project),
                      'wall_seconds': diagnostic['wall_seconds']}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--synthetic', action='store_true')
    parser.add_argument('--mode', choices=['stop', 'early-stop', 'cancel', 'early-cancel', 'unserviced-cancel', 'source-loss', 'sink-loss', 'writer-fail', 'hash-fail'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--failure-output', type=Path, required=True)
    args = parser.parse_args()
    run(args.binary.resolve(), not args.synthetic, args.mode, args.output, args.failure_output)


if __name__ == '__main__':
    main()
