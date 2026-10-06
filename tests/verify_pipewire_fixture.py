#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in owned PipeWire integration, including graph/default preservation.

Uses an existing user daemon. Does not choose/change a hardware route, default,
rate or quantum. Run separately from CTest; new audio activity by the user may
invalidate preservation observations, which must then be reported honestly.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = ('sc-daw-fixture-', 'sc-daw-playback-', 'sc-daw-recording-')


def snapshot():
    objects = json.loads(subprocess.run(['pw-dump'], check=True, capture_output=True,
                                        text=True, timeout=5).stdout)
    owned = {o['id'] for o in objects if o['type'] == 'PipeWire:Interface:Node' and
             o.get('info', {}).get('props', {}).get('node.name', '').startswith(PREFIXES)}
    defaults = []
    links = {}
    owned_edges = []
    for o in objects:
        if o['type'] == 'PipeWire:Interface:Metadata':
            for m in o.get('metadata', []):
                if m.get('key', '').startswith('default.'):
                    defaults.append(m)
        if o['type'] == 'PipeWire:Interface:Link':
            p = o['info']['props']
            ends = (p['link.output.node'], p['link.input.node'])
            if any(n in owned for n in ends):
                if not all(n in owned for n in ends):
                    raise AssertionError('Owned fixture has a link to an unowned node')
                owned_edges.append(ends)
            else:
                links[str(p['object.serial'])] = p
    defaults = sorted(defaults, key=lambda m: (m.get('subject', 0), m['key']))
    return {'defaults': defaults, 'links': links, 'owned': owned, 'edges': owned_edges}


def unchanged(before, observed):
    assert before['defaults'] == observed['defaults'], 'System defaults changed during fixture'
    for serial, properties in before['links'].items():
        assert observed['links'].get(serial) == properties, 'Pre-existing playback link changed'


def thread_schedulers(pid):
    """Read only owned fixture thread policies on this control process."""
    samples = []
    try:
        tasks = list((Path('/proc') / str(pid) / 'task').iterdir())
    except OSError:
        return samples
    for task in tasks:
        try:
            tid = int(task.name)
            policy = os.sched_getscheduler(tid)
            reset_flag = getattr(os, 'SCHED_RESET_ON_FORK', 0x40000000)
            samples.append({'tid': tid, 'name': (task / 'comm').read_text().strip(),
                            'policy': policy, 'base_policy': policy & ~reset_flag,
                            'reset_on_fork': bool(policy & reset_flag),
                            'priority': os.sched_getparam(tid).sched_priority,
                            'nice': os.getpriority(os.PRIO_PROCESS, tid)})
        except (OSError, ValueError):
            pass  # A short-lived owned thread may exit between these reads.
    return samples


def run_fixture(binary, mode, diagnostics=None):
    diagnostics = diagnostics if diagnostics is not None else {}
    diagnostics.update(mode=mode, scheduler_samples=[])
    before = snapshot()
    assert not before['owned'], 'Another owned fixture is already running'
    observed_graphs, linked_graphs = 0, 0
    with tempfile.TemporaryDirectory(prefix='s5-pw-', dir=ROOT / '.cache') as tmp:
        project = Path(tmp) / 'Enregistrement – Δοκιμή'
        child = subprocess.Popen([binary, project, mode], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 30
            while child.poll() is None:
                if time.monotonic() >= deadline:
                    child.kill()
                    stdout, stderr = child.communicate(timeout=5)
                    diagnostics.update(stdout=stdout, stderr=stderr, exit_code=child.returncode,
                                       observation_timeout=True)
                    raise AssertionError(f'Native fixture timed out: {stderr}; stdout: {stdout}')
                current = snapshot()
                unchanged(before, current)
                observed_graphs += 1
                linked_graphs += bool(current['edges'])
                if current['edges'] and not diagnostics['scheduler_samples']:
                    diagnostics['scheduler_samples'] = thread_schedulers(child.pid)
                time.sleep(.1)
            stdout, stderr = child.communicate(timeout=2)
            diagnostics.update(stdout=stdout, stderr=stderr, exit_code=child.returncode,
                               graph_snapshots=observed_graphs, linked_snapshots=linked_graphs)
            if child.returncode:
                raise AssertionError(f'Native fixture failed ({child.returncode}): {stderr}')
            result = json.loads(stdout)
            assert result['mode'] == mode and result['owned_nodes_only']
            if not result.get('activation_failed', False):
                assert linked_graphs > 0, 'No active owned links observed'
            deadline = time.monotonic() + 3
            while True:
                after = snapshot()
                unchanged(before, after)
                if not after['owned']:
                    break
                assert time.monotonic() < deadline, 'Fixture nodes leaked after shutdown'
                time.sleep(.05)
            assert not after['edges']
            result['routing_observations'] = {
                'graph_snapshots_during_run': observed_graphs,
                'snapshots_with_owned_links': linked_graphs,
                'no_observed_owned_to_unowned_links': True,
                'pre_existing_links_preserved': len(before['links']),
                'default_metadata_unchanged': True,
                'default_metadata_sha256': hashlib.sha256(
                    json.dumps(before['defaults'], sort_keys=True).encode()).hexdigest(),
                'owned_nodes_and_links_removed': True,
            }
            result['native_diagnostic'] = stderr.strip()
            result['scheduler_samples'] = diagnostics['scheduler_samples']
            return result
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate(timeout=5)
            # Preserve cleanup/default observations on failure as well as success.
            try:
                after = snapshot()
                unchanged(before, after)
                diagnostics['cleanup_observation'] = {
                    'default_metadata_unchanged': True,
                    'pre_existing_links_preserved': len(before['links']),
                    'owned_nodes_remaining': len(after['owned']),
                    'owned_links_remaining': len(after['edges'])}
            except (AssertionError, subprocess.SubprocessError, ValueError) as error:
                diagnostics['cleanup_error'] = str(error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path,
                        default=ROOT / '.cache/build-core/sc-pipewire-fixture')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--modes', nargs='+', default=['normal', 'disconnect'])
    parser.add_argument('--failure-output', type=Path,
                        help='Retain failed case diagnostics without turning failure into success')
    args = parser.parse_args()
    (ROOT / '.cache').mkdir(exist_ok=True)
    results = []
    for mode in args.modes:
        diagnostics = {}
        try:
            results.append(run_fixture(args.binary.resolve(), mode, diagnostics))
        except (AssertionError, subprocess.SubprocessError) as error:
            if args.failure_output:
                args.failure_output.write_text(json.dumps(
                    {'failure': str(error), 'diagnostics': diagnostics,
                     'completed_cases': results}, indent=2, ensure_ascii=False) + '\n')
            raise
    encoded = json.dumps(results, indent=2, ensure_ascii=False) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
