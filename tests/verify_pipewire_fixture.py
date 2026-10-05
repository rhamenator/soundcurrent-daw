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
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PREFIX = 'sc-daw-fixture-'


def snapshot():
    objects = json.loads(subprocess.run(['pw-dump'], check=True, capture_output=True,
                                        text=True, timeout=5).stdout)
    owned = {o['id'] for o in objects if o['type'] == 'PipeWire:Interface:Node' and
             o.get('info', {}).get('props', {}).get('node.name', '').startswith(PREFIX)}
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


def run_fixture(binary, mode):
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
                assert time.monotonic() < deadline, 'Native fixture timed out'
                current = snapshot()
                unchanged(before, current)
                observed_graphs += 1
                linked_graphs += bool(current['edges'])
                time.sleep(.1)
            stdout, stderr = child.communicate(timeout=2)
            if child.returncode:
                raise AssertionError(f'Native fixture failed ({child.returncode}): {stderr}')
            result = json.loads(stdout)
            assert result['mode'] == mode and result['owned_nodes_only']
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
            return result
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path,
                        default=ROOT / '.cache/build-core/sc-pipewire-fixture')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    (ROOT / '.cache').mkdir(exist_ok=True)
    results = [run_fixture(args.binary.resolve(), m) for m in ('normal', 'disconnect')]
    encoded = json.dumps(results, indent=2, ensure_ascii=False) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
