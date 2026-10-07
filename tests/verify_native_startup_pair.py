#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify one controlled startup mechanism; historical causes remain unproven."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_native_port_handoff import analyze as handoff_analysis
from verify_pipewire_manual_priority import priority
from verify_pipewire_manual_trace import analyze as marker_analysis


def gate_case(root):
    gate = json.loads((root / 'native-startup-gate.json').read_text())
    h = json.loads((root / 'native-port-handoff.json').read_text())
    markers = {role: json.loads((root / (role + '-port-markers.json')).read_text())
               for role in ['source', 'owner']}
    coverage = handoff_analysis(h, markers, gate['policy'] == 'defer-unready')
    source = next(f for f in h['filters'] if 'manual-fault-source-' in f['name'])
    clock = gate['clock']
    rows = [row for row in source['rows'] if all(row[k] == v for k, v in clock.items())]
    assert len(rows) == 1
    row = rows[0]
    q = row['queries'][23]
    ordinal = source['rows'].index(row)
    return {'gate': gate, 'query': q, 'marker': markers['source']['rows'][ordinal]['ports'][23],
            'coverage': coverage}


def verify_pair(a, b, failed_trace, passed_trace, raw, child):
    for case, policy in [(a, 'observe'), (b, 'defer-unready')]:
        g, q, marker = case['gate'], case['query'], case['marker']
        assert g['policy'] == policy and g['test_only'] is True and g['selected_channel'] == 23
        assert all(g[k] is True for k in ['entered', 'entered_outside_rt', 'completed', 'released'])
        assert g['timed_out'] is False and g['io_known'] is False and g['live_buffers'] == 1
        assert q['port'] == 23 and q['io_known'] is False and q['live_buffers'] == 1
        deferred = policy == 'defer-unready'
        assert g['api_suppressed'] is deferred and q['api_suppressed'] is deferred
        assert g['returned'] is (not deferred) and q['returned'] is (not deferred)
        assert g['known_buffer'] is (not deferred) and q['known_buffer'] is (not deferred)
        assert marker['seen'] is True and marker['channel'] == 23
        assert marker['present'] is (not deferred) and marker['sampled'] is (not deferred)
        assert g['clock']['duration'] == q['frames'] == marker['frames']
    assert a['gate']['clock']['duration'] == b['gate']['clock']['duration']
    quantum = a['gate']['clock']['duration']
    mismatches = failed_trace['marker_mismatches']
    advancing = failed_trace['coverage']['owner']['active_rows']
    assert failed_trace['qualified_trace'] is False and len(mismatches) == advancing > 0
    assert len(failed_trace['problems']) == advancing
    assert all(p == f'owner row{n}: source/received markers differ'
               for n, p in enumerate(failed_trace['problems']))
    assert all(m['role'] == 'owner' and m['channel'] == 23 and
               m['matching_diagnostic_shifts'] == [-quantum] for m in mismatches)
    assert passed_trace['qualified_trace'] is True and not passed_trace['problems']
    assert not passed_trace['marker_mismatches']
    affected = [r for r in raw['lanes'] if r['unshifted_mismatches']]
    assert len(affected) == 1
    lane = affected[0]
    assert lane['lane'] == 17 and lane['source_channel'] == 23
    assert lane['unshifted_mismatches'] == lane['frames'] > 0
    assert lane['diagnostic_shift_mismatches'][str(-quantum)] == 0
    assert raw['compensation_applied'] is False and raw['replay'] is False
    assert raw['samples'] == child['raw_samples_verified'] == child['recovered_samples_verified']
    priority(child)  # Entire original waveform/recovery/timing/cycle/priority gates.
    assert a['coverage']['roles']['source']['sdk_queries_skipped_by_test_policy'] == 0
    assert b['coverage']['roles']['source']['sdk_queries_skipped_by_test_policy'] >= 1
    return {'controlled_startup_mechanism_qualified': True,
            'private_queue_depth_observed': False, 'historical46_cause_established': False,
            'historical37_cause_established': False, 'production_fix_implemented': False,
            'quantum': quantum, 'failed_advancing_callbacks': advancing,
            'affected_durable_samples': lane['frames'], 'raw_samples_each_case': raw['samples'],
            'output_samples_verified_in_counterfactual': child['output_samples_verified']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--original', type=Path, required=True)
    p.add_argument('--counterfactual-receipt', type=Path, required=True)
    p.add_argument('--original-raw-analysis', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    receipt = json.loads(args.counterfactual_receipt.read_text())
    assert receipt['exit_code'] == 0 and 'error' not in receipt
    counterfactual = Path(receipt['project_directory'])
    a, b = gate_case(args.original), gate_case(counterfactual)
    failed, passed = marker_analysis(args.original), marker_analysis(counterfactual)
    raw = json.loads(args.original_raw_analysis.read_text())
    result = verify_pair(a, b, failed, passed, raw, receipt['child_result'])
    result.update(original_case=a, counterfactual_case=b,
                  original_marker_analysis=failed, counterfactual_marker_analysis=passed)
    result['input_sha256'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in [args.counterfactual_receipt, args.original_raw_analysis]}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in
                     ['original_case', 'counterfactual_case', 'original_marker_analysis',
                      'counterfactual_marker_analysis', 'input_sha256']}))


if __name__ == '__main__':
    main()
