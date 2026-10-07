#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check joined public API coverage; observations do not establish a delay cause."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

CLOCK = ['id', 'cycle', 'position', 'duration', 'nsec', 'rate_numerator',
         'rate_denominator', 'delay']


def analyze(handoff, markers):
    assert handoff['test_only'] is True and handoff['public_api_observer'] is True
    assert len(handoff['filters']) == 3
    assert set(markers) == {'owner', 'source'}
    report = {}
    for f in handoff['filters']:
        name = f['name']
        role = ('source' if name.startswith('sc-daw-fixture-manual-fault-source-') else
                'sink' if name.startswith('sc-daw-fixture-manual-fault-sink-') else
                'owner' if name.startswith('sc-daw-recording-manual-') else None)
        assert role is not None and role not in report
        for key in ['dropped', 'query_overflows', 'unknown_queries', 'unknown_events',
                    'outer_allocations', 'outer_frees', 'outer_locks']:
            assert f[key] == 0, (role, key)
        rows, ports = f['rows'], f['ports']
        inputs, outputs = {'source': (0, 32), 'owner': (32, 2), 'sink': (2, 0)}[role]
        assert len(ports) == inputs + outputs
        for index, port in enumerate(ports):
            assert port == {'index': index, 'channel': index if index < inputs else index - inputs,
                            'input': index < inputs}
        assert f['calls'] == len(rows) > 0
        timing = f['wrapper_timing']
        assert timing['calls'] == len(rows)
        for key in ['complete_timing_coverage', 'complete_cpu_coverage',
                    'complete_thread_usage_coverage', 'finite_deadline_thresholds_met']:
            assert timing[key] is True, (role, key)
        marker = markers[role] if role != 'sink' else None
        if role != 'sink':
            assert isinstance(marker, dict) and marker
            assert marker['source'] is (role == 'source')
            assert marker['channels'] == 32
            assert marker['expected_buffer_calls'] == len(ports)
            assert marker['calls'] == marker['retained'] == len(rows)
            assert len(marker['rows']) == len(rows)
            assert marker['dropped'] == marker['identity_overflows'] == 0
        states = Counter()
        notable = []
        seen = set()
        for ordinal, row in enumerate(rows):
            assert row['clock_known'] is True
            identity = tuple(row[k] for k in ['id', 'cycle', 'position', 'duration'])
            assert identity not in seen
            seen.add(identity)
            assert 0 < row['duration'] <= 65536 and row['rate_numerator'] > 0
            assert row['rate_denominator'] > 0
            assert row['calls'] == len(row['queries']) == len(ports)
            if marker:
                reference = marker['rows'][ordinal]
                assert len(reference['ports']) == 32
                assert reference['clock_known'] is True
                assert all(row[k] == reference[k] for k in CLOCK)
                assert reference['buffer_calls'] == row['calls']
            for index, q in enumerate(row['queries']):
                assert q['port'] == index and q['frames'] == row['duration']
                assert 0 <= q['live_buffers'] <= 64
                if q['io_known']:
                    assert q['io_id'] == 1 and q['io_bytes'] >= 8
                if q['returned']:
                    assert q['known_buffer'] is True
                    assert 0 <= q['buffer'] < 64 and q['live_buffers'] > 0
                    assert q['maximum_bytes'] >= q['frames'] * 4
                else:
                    assert q['known_buffer'] is False and q['maximum_bytes'] == 0
                    assert q['buffer'] == 4294967295
                if marker and index < marker['channels']:
                    p = reference['ports'][index]
                    assert p['seen'] is True and p['frames'] == q['frames']
                    assert q['returned'] == p['present']
                    assert p['channel'] == index
                states[(q['io_known'], q['io_status'], q['returned'], q['live_buffers'])] += 1
                # Pre-API output HAVE_DATA or an unavailable IO area is recorded,
                # not interpreted as proof of a private FIFO depth or publication.
                if (not ports[index]['input'] and q['returned'] and
                        (not q['io_known'] or q['io_status'] == 2)):
                    notable.append({'row': ordinal, 'channel': ports[index]['channel'],
                                    **{k: row[k] for k in CLOCK}, 'query': q})
        report[role] = {'callbacks': len(rows), 'queries': sum(states.values()),
                        'marker_correspondence_checked': marker is not None,
                        'query_states': [dict(io_known=a, io_status=b, returned=c,
                                              live_buffers=d, count=count)
                                         for (a, b, c, d), count in sorted(states.items())],
                        'notable_pre_api_output_queries': notable,
                        'wrapper_maximum_ns': timing['maximum_ns'],
                        'wrapper_maximum_cpu_ns': timing['maximum_cpu_ns']}
    return {'public_observation_coverage_qualified': True,
            'waveform_qualification_performed_here': False,
            'private_queue_state_observed': False, 'delay_cause_established': False,
            'timing_scope': 'row bookkeeping, original callback, DSP hooks and cleanup; '
                            'bounded observer lookup audited but excluded from timing',
            'roles': report}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    files = {name: args.project / name for name in ['native-port-handoff.json',
             'owner-port-markers.json', 'source-port-markers.json']}
    h = json.loads(files['native-port-handoff.json'].read_text())
    markers = {role: json.loads(files[role + '-port-markers.json'].read_text())
               for role in ['owner', 'source']}
    result = analyze(h, markers)
    result['input_sha256'] = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                              for name, path in files.items()}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'public_observation_coverage_qualified': True,
                      'callbacks': {r: v['callbacks'] for r, v in result['roles'].items()},
                      'delay_cause_established': False}))


if __name__ == '__main__':
    main()
