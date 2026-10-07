#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Join repeated manual stage costs to the original full native acceptance receipt."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_pipewire_manual import qualify

STAGES = ['raw_capture', 'eq_drivers', 'mix_including_eq']
CLOCK = {'clock_position': 'position', 'clock_cycle': 'cycle', 'clock_nsec': 'nsec',
         'clock_id': 'id', 'quantum': 'duration', 'rate_numerator': 'rate_numerator',
         'rate_denominator': 'rate_denominator'}


def cost(c, individual=False):
    assert c['unknown_intervals'] == 0
    assert all(isinstance(c[k], int) and c[k] >= 0 for k in ['calls', 'wall_ns', 'cpu_ns'])
    assert c['cpu_ns'] <= c['wall_ns']
    if individual:
        assert c['maximum_call_known'] is (c['calls'] > 0)
        assert 0 <= c['maximum_call_cpu_ns'] <= c['maximum_call_wall_ns'] <= c['wall_ns']
        if c['calls']:
            assert 0 <= c['maximum_call_ordinal'] < c['calls']
        else:
            assert c['wall_ns'] == c['cpu_ns'] == 0


def verify(receipt, stages):
    assert receipt['exit_code'] == 0 and 'error' not in receipt and receipt['native'] is True
    child = receipt['child_result']
    qualify(child, True, receipt['late_service'])  # Entire original acceptance contract.
    route = receipt['routing_observations']
    assert route['default_metadata_unchanged'] and route['no_observed_owned_to_unowned_links']
    assert route['owned_nodes_remaining'] == route['owned_links_remaining'] == 0
    assert stages['test_only_linker_wrappers'] is True and stages['nested_costs_inclusive'] is True
    owner = child['callback_timing']['owner']
    cost(stages['total_bridge'])
    assert stages['total_bridge']['calls'] == owner['calls'] > 4
    assert set(stages['total_stages']) == set(STAGES)
    for c in stages['total_stages'].values():
        cost(c)
    assert stages['total_stages']['eq_drivers']['calls'] == 33 * stages['total_stages']['mix_including_eq']['calls']
    worst = stages['worst_bridge_callbacks']
    assert len(worst) == 4
    seen, previous = set(), None
    for row in worst:
        identity = tuple(row[k] for k in ['clock_id', 'clock_cycle', 'clock_position', 'quantum'])
        assert identity not in seen
        seen.add(identity)
        assert row['clock_id'] == owner['maximum_clock']['id']
        assert row['rate_numerator'] == 1 and row['rate_denominator'] == 48000
        assert 0 < row['quantum'] <= 2048 and row['status'] in [1, 2, 3]
        cost(row['bridge'])
        assert row['bridge']['calls'] == 1 and row['bridge']['wall_ns'] > 0
        if previous is not None:
            assert row['bridge']['wall_ns'] <= previous
        previous = row['bridge']['wall_ns']
        assert set(row['stages']) == set(STAGES)
        for c in row['stages'].values():
            cost(c, True)
            assert c['wall_ns'] <= row['bridge']['wall_ns']
        assert row['stages']['eq_drivers']['calls'] == 33 * row['stages']['mix_including_eq']['calls']
        # EQ is included within mix; do not double-count it as disjoint work.
        assert row['stages']['eq_drivers']['wall_ns'] <= row['stages']['mix_including_eq']['wall_ns']
    matching = [r for r in worst if all(r[k] == owner['maximum_clock'][v] for k, v in CLOCK.items())]
    assert len(matching) == 1, 'Owner maximum callback not retained by stage observer'
    selected = matching[0]
    assert selected['bridge']['wall_ns'] <= owner['maximum_ns']
    assert selected['bridge']['cpu_ns'] <= owner['maximum_callback_cpu_ns']
    dominant = max(STAGES, key=lambda k: selected['stages'][k]['cpu_ns'])
    return {'bounded_repeated_native_workflow_qualified': True,
            'complete_bridge_observation_coverage': True,
            'same_clock_owner_maximum_joined': True,
            'selected_owner_clock': owner['maximum_clock'], 'selected_bridge': selected,
            'dominant_inclusive_stage_in_selected_callback': dominant,
            'raw_samples_verified': child['raw_samples_verified'],
            'output_samples_verified': child['output_samples_verified'],
            'maximum_sample_difference': child['maximum_sample_difference'],
            'historical48_failure_reproduced': False,
            'historical48_cpu_cause_established': False,
            'sustained_performance_qualified': False,
            'observer_cost_removed_or_estimated': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--project', type=Path, help='Relocated original project; no receipt rewrite')
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    r = json.loads(args.receipt.read_text())
    root = args.project if args.project is not None else Path(r['project_directory'])
    source = root / 'repeated-manual-processing-stages.json'
    result = verify(r, json.loads(source.read_text()))
    result['input_sha256'] = {'receipt': hashlib.sha256(args.receipt.read_bytes()).hexdigest(),
                              'stages': hashlib.sha256(source.read_bytes()).hexdigest()}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in
                      ['selected_bridge', 'selected_owner_clock', 'input_sha256']}))


if __name__ == '__main__':
    main()
