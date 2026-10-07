#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify production buffer readiness under an explicit test-only startup hold."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_native_port_handoff import analyze as handoff_analysis
from verify_pipewire_manual_priority import priority
from verify_pipewire_manual_trace import analyze as marker_analysis


def verify(gate, handoff, markers, trace, child):
    assert handoff['acquisition_version'] == 2
    assert all(m['acquisition_version'] == 2 for m in markers.values())
    coverage = handoff_analysis(handoff, markers)  # API suppression always refused.
    assert gate['test_only'] is True and gate['policy'] == 'production-ready'
    assert gate['selected_channel'] == 23
    assert all(gate[k] is True for k in ['entered', 'entered_outside_rt', 'completed', 'released'])
    assert gate['timed_out'] is False and gate['api_suppressed'] is False
    assert gate['io_known'] is False and gate['live_buffers'] == 1
    assert gate['returned'] is False and gate['known_buffer'] is False
    assert set(gate['clock']) == {'id', 'cycle', 'position', 'duration', 'nsec',
                                 'rate_numerator', 'rate_denominator'}
    source = next(f for f in handoff['filters'] if 'manual-fault-source-' in f['name'])
    rows = [r for r in source['rows'] if all(r[k] == v for k, v in gate['clock'].items())]
    assert len(rows) == 1
    row = rows[0]
    ordinal = source['rows'].index(row)
    q = row['queries'][23]
    m = markers['source']['rows'][ordinal]['ports'][23]
    assert q['port'] == 23 and q['frames'] == gate['clock']['duration']
    assert q['live_buffers'] == 1 and q['io_known'] is False
    assert q['returned'] is False and q['native_returned'] is False
    assert q['api_suppressed'] is False and q['acquisition_status'] == 0
    assert q['sdk_dequeues'] == q['sdk_queues'] == 0
    assert m['seen'] is True and m['present'] is False and m['sampled'] is False
    assert any(r['queries'][23]['io_known'] and r['queries'][23]['returned'] and
               r['queries'][23]['sdk_dequeues'] == r['queries'][23]['sdk_queues'] == 1
               for r in source['rows'][ordinal + 1:]), 'Selected port never becomes usable'
    assert trace['qualified_trace'] is True and not trace['problems'] and not trace['marker_mismatches']
    priority(child)  # Original full waveform, recovery, RT, timing and priority gates.
    for role in ['source', 'owner', 'sink']:
        assert coverage['roles'][role]['callbacks'] == child['callback_timing'][role]['calls']
        assert coverage['roles'][role]['sdk_queries_skipped_by_test_policy'] == 0
    return {'production_startup_readiness_qualified': True,
            'test_only_notification_hold': True, 'api_suppression_applied': False,
            'selected_port_skipped_sdk_dequeue': True, 'later_port_use_qualified': True,
            'full_32_channel_marker_coverage': True,
            'raw_samples_verified': child['raw_samples_verified'],
            'recovered_samples_verified': child['recovered_samples_verified'],
            'output_samples_verified': child['output_samples_verified'],
            'original_fixture_gates_met': True, 'historical_failure_causes_established': False,
            'sustained_performance_qualified': False, 'gate_clock': gate['clock'],
            'selected_query': q, 'coverage': coverage}


def load(receipt_path, project=None):
    receipt = json.loads(receipt_path.read_text())
    assert receipt['exit_code'] == 0 and 'error' not in receipt
    root = project if project is not None else Path(receipt['project_directory'])
    files = {name: root / name for name in ['native-startup-gate.json',
             'native-port-handoff.json', 'owner-port-markers.json', 'source-port-markers.json',
             'manual-processing-stages.json']}
    gate = json.loads(files['native-startup-gate.json'].read_text())
    handoff = json.loads(files['native-port-handoff.json'].read_text())
    markers = {role: json.loads(files[role + '-port-markers.json'].read_text())
               for role in ['owner', 'source']}
    return (gate, handoff, markers, marker_analysis(root), receipt['child_result']), files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--project', type=Path, help='Relocated original project; receipt remains unchanged')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inputs, files = load(args.receipt, args.project)
    result = verify(*inputs)
    files['receipt'] = args.receipt
    result['input_sha256'] = {name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for name, p in files.items()}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in
                      ['coverage', 'selected_query', 'input_sha256']}))


if __name__ == '__main__':
    main()
