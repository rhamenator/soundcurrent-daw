#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in mutation checks using an actual retained, qualified trace receipt."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import tempfile
from verify_pipewire_manual_trace import analyze, markers


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    receipt = json.loads(args.receipt.read_text())
    project = Path(receipt['project_directory'])
    assert analyze(project)['qualified_trace'] and receipt['exit_code'] == 0 and 'error' not in receipt
    names = ['source-port-markers', 'owner-port-markers', 'manual-processing-stages']
    original = {name: json.loads((project / (name + '.json')).read_text()) for name in names}
    observed = []
    with tempfile.TemporaryDirectory(prefix='sc-marker-verifier-') as root:
        root = Path(root)
        for case in ['source-bit', 'owner-bit', 'owner-quantum-delay', 'missing-port',
                     'unknown-clock', 'drop', 'identity-overflow', 'bridge-count',
                     'unknown-stage', 'worst-clock', 'stage-ordinal', 'missing-source-clock',
                     'consumed-null-source', 'consumed-partial-source']:
            data = copy.deepcopy(original)
            source = data[names[0]]
            owner = data[names[1]]
            stages = data[names[2]]
            row = next(r for r in owner['rows'] if r['after'] > r['before'])
            source_row = next(r for r in source['rows'] if
                              all(r[k] == row[k] for k in ['id', 'cycle', 'position', 'duration']))
            if case == 'source-bit':
                source_row['ports'][0]['bits'][0] ^= 1
            elif case == 'owner-bit':
                row['ports'][0]['bits'][0] ^= 1
            elif case == 'owner-quantum-delay':
                row['ports'][19]['bits'] = markers(row['position'] - row['duration'], row['duration'], 19)
            elif case == 'missing-port':
                row['ports'][0]['sampled'] = False
            elif case == 'unknown-clock':
                row['clock_known'] = False
            elif case == 'drop':
                owner['dropped'] += 1
                owner['calls'] += 1
            elif case == 'identity-overflow':
                owner['identity_overflows'] += 1
            elif case == 'bridge-count':
                stages['total_bridge']['calls'] += 1
            elif case == 'unknown-stage':
                stages['total_stages']['raw_capture']['unknown_intervals'] += 1
            elif case == 'worst-clock':
                stages['worst_bridge_callbacks'][0]['clock_id'] = -1
            elif case == 'stage-ordinal':
                cost = next(cost for r in stages['worst_bridge_callbacks'] for cost in r['stages'].values()
                            if cost['calls'])
                cost['maximum_call_ordinal'] = cost['calls']
            elif case == 'missing-source-clock':
                source_row['id'] = -1
            elif case in ['consumed-null-source', 'consumed-partial-source']:
                for port in source_row['ports'][:32 if case == 'consumed-null-source' else 1]:
                    port['present'] = port['sampled'] = False
            for name in names:
                (root / (name + '.json')).write_text(json.dumps(data[name]))
            try:
                result = analyze(root)
                refused = not result['qualified_trace']
            except (AssertionError, KeyError, ValueError):
                refused = True
                result = None
            assert refused, case + ' incorrectly accepted'
            if case == 'owner-quantum-delay':
                assert result is not None and any(
                    m['channel'] == 19 and -row['duration'] in m['matching_diagnostic_shifts']
                    for m in result['marker_mismatches'])
            observed.append({'mutation': case, 'refused': True})
    args.output.write_text(json.dumps({'reference_sha256': hashlib.sha256(args.receipt.read_bytes()).hexdigest(),
                                      'refusals': observed}, indent=2) + '\n')
    print(json.dumps({'refused_mutations': len(observed), 'output': str(args.output)}))


if __name__ == '__main__':
    main()
