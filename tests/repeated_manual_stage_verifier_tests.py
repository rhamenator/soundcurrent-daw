#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse altered retained repeated-take evidence without replaying audio."""
import argparse
import copy
import json
from pathlib import Path
from verify_repeated_manual_stages import verify


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--project', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    r = json.loads(args.receipt.read_text())
    root = args.project if args.project is not None else Path(r['project_directory'])
    s = json.loads((root / 'repeated-manual-processing-stages.json').read_text())
    verify(r, s)
    cases = [(name, 'stage', path, value) for name, path, value in [
        ('undeclared wrapper', ['test_only_linker_wrappers'], False),
        ('wrong cost semantics', ['nested_costs_inclusive'], False),
        ('missing bridge coverage', ['total_bridge', 'calls'], 1),
        ('unknown interval', ['total_bridge', 'unknown_intervals'], 1),
        ('wrong EQ coverage', ['total_stages', 'eq_drivers', 'calls'], 1),
        ('invalid clock', ['worst_bridge_callbacks', 0, 'clock_id'], 0),
        ('missing same-clock peak', ['worst_bridge_callbacks', 0, 'clock_position'], 0),
        ('invalid quantum', ['worst_bridge_callbacks', 0, 'quantum'], 0),
        ('invalid rate', ['worst_bridge_callbacks', 0, 'rate_denominator'], 44100),
        ('unknown selected cost', ['worst_bridge_callbacks', 0, 'bridge', 'unknown_intervals'], 1),
        ('invalid selected ordinal', ['worst_bridge_callbacks', 0, 'stages', 'eq_drivers', 'maximum_call_ordinal'], 99999)]]
    cases += [(name, 'receipt', path, value) for name, path, value in [
        ('failed original run', ['exit_code'], 1),
        ('raw extent omitted', ['child_result', 'raw_samples_verified'], 1),
        ('full output omitted', ['child_result', 'output_samples_verified'], 1),
        ('timing gate false', ['child_result', 'callback_timing', 'owner', 'finite_deadline_thresholds_met'], False),
        ('unknown maximum clock', ['child_result', 'callback_timing', 'owner', 'maximum_clock', 'position'], 0),
        ('omitted Save/reopen', ['child_result', 'save_reopen'], False),
        ('omitted grouped Undo/Redo', ['child_result', 'grouped_undo_redo'], False),
        ('route leak', ['routing_observations', 'owned_nodes_remaining'], 1)]]
    refused = []
    for name, which, path, value in cases:
        receipt, stages = copy.deepcopy(r), copy.deepcopy(s)
        parent = stages if which == 'stage' else receipt
        for key in path[:-1]:
            parent = parent[key]
        parent[path[-1]] = value
        try:
            verify(receipt, stages)
        except (AssertionError, KeyError, ValueError):
            refused.append(name)
        else:
            raise AssertionError('Altered evidence accepted: ' + name)
    for name, change in [('missing peak', lambda a: a['worst_bridge_callbacks'].pop()),
                         ('extra peak', lambda a: a['worst_bridge_callbacks'].append(copy.deepcopy(a['worst_bridge_callbacks'][0]))),
                         ('duplicate clock', lambda a: a['worst_bridge_callbacks'].__setitem__(1, copy.deepcopy(a['worst_bridge_callbacks'][0])))]:
        altered = copy.deepcopy(s)
        change(altered)
        try:
            verify(r, altered)
        except (AssertionError, KeyError, ValueError):
            refused.append(name)
        else:
            raise AssertionError('Coverage mutation accepted: ' + name)
    result = {'original_retained_baseline_qualified': True,
              'native_audio_replayed': False, 'mutations_refused': refused}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'mutations_refused': len(refused)}))


if __name__ == '__main__':
    main()
