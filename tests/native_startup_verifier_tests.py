#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse altered controlled-pair evidence without rerunning native audio."""
import argparse
import copy
import json
from pathlib import Path
from verify_native_startup_pair import gate_case, verify_pair
from verify_pipewire_manual_trace import analyze


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original', type=Path, required=True)
    p.add_argument('--counterfactual-receipt', type=Path, required=True)
    p.add_argument('--counterfactual-project', type=Path,
                   help='Relocated project directory; leaves the original receipt unchanged')
    p.add_argument('--original-raw-analysis', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    receipt = json.loads(args.counterfactual_receipt.read_text())
    root = args.counterfactual_project or Path(receipt['project_directory'])
    baseline = [gate_case(args.original), gate_case(root), analyze(args.original), analyze(root),
                json.loads(args.original_raw_analysis.read_text()), receipt['child_result']]
    verify_pair(*baseline)
    cases = [(name, [0, 'gate', key], value) for name, key, value in [
        ('no startup hold', 'entered', False), ('hold on RT', 'entered_outside_rt', False),
        ('unobserved query', 'completed', False), ('unreleased hold', 'released', False),
        ('timeout', 'timed_out', True), ('wrong source channel', 'selected_channel', 22),
        ('IO was already available', 'io_known', True), ('no allocated buffer', 'live_buffers', 0)]]
    cases += [('original getter suppressed', [0, 'query', 'api_suppressed'], True),
              ('counterfactual getter not deferred', [1, 'query', 'api_suppressed'], False),
              ('counterfactual returned a buffer', [1, 'query', 'returned'], True),
              ('counterfactual marker present', [1, 'marker', 'present'], True),
              ('different clock quantum', [1, 'gate', 'clock', 'duration'], 512),
              ('original unexpectedly qualified', [2, 'qualified_trace'], True),
              ('wrong delayed channel', [2, 'marker_mismatches', 0, 'channel'], 24),
              ('wrong diagnostic shift', [2, 'marker_mismatches', 0, 'matching_diagnostic_shifts'], [256]),
              ('counterfactual marker failure', [3, 'qualified_trace'], False),
              ('raw compensation', [4, 'compensation_applied'], True),
              ('raw replay', [4, 'replay'], True),
              ('lost raw sample coverage', [4, 'samples'], 1),
              ('callback allocation', [5, 'rt_allocations'], 1)]
    refused = []
    for name, path, value in cases:
        altered = copy.deepcopy(baseline)
        parent = altered
        for key in path[:-1]: parent = parent[key]
        assert path[-1] in parent, 'Mutation must target an existing evidence field'
        parent[path[-1]] = value
        try: verify_pair(*altered)
        except (AssertionError, KeyError, ValueError): refused.append(name)
        else: raise AssertionError('Altered startup pair accepted: ' + name)
    args.output.write_text(json.dumps({'native_audio_repeated': False,
                                      'mutations_refused': refused}, indent=2) + '\n')
    print(json.dumps({'mutations_refused': len(refused)}))


if __name__ == '__main__':
    main()
