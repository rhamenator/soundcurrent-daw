#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect joined manual-fixture traces; never compensate channel offsets."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MASK = (1 << 64) - 1


def signal_bits(at, channel):
    value = at ^ ((channel + 1) * 0x9e3779b97f4a7c15 & MASK)
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9 & MASK
    value = (value ^ (value >> 27)) * 0x94d049bb133111eb & MASK
    value ^= value >> 31
    return struct.unpack('<I', struct.pack('<f', ((value >> 40) - 0x800000) * 2.0 ** -22))[0]


def markers(position, duration, channel):
    delay = [4097, 0, 41, 200][(channel * 23 % 32) % 4]
    count = min(duration, 4)
    return [signal_bits(position + f - delay, channel) if position + f >= delay else 0
            for f in list(range(count)) + list(range(duration - count, duration))]


def key(row):
    return tuple(row[k] for k in ['id', 'cycle', 'position', 'duration'])


def analyze(project):
    files = {name: project / (name + '.json') for name in
             ['source-port-markers', 'owner-port-markers', 'manual-processing-stages']}
    traces = {name: json.loads(p.read_text()) for name, p in files.items()}
    source, owner, stages = [traces[name] for name in files]
    problems, mismatches = [], []
    coverage = {}
    generated = {}
    for role, trace in [('source', source), ('owner', owner)]:
        assert trace['test_only'] and trace['source'] == (role == 'source')
        assert trace['channels'] == 32 and trace['admitted_rows'] == 8192
        assert trace['expected_buffer_calls'] == (32 if role == 'source' else 34)
        assert trace['retained'] == len(trace['rows']) <= trace['admitted_rows']
        assert trace['calls'] == trace['retained'] + trace['dropped']
        if trace['dropped'] or trace['identity_overflows']:
            problems.append(role + ': dropped rows or exhausted buffer identities')
        stats = {'callbacks': trace['calls'], 'observed_ports': 0, 'sampled_ports': 0,
                 'active_rows': 0, 'nonadvancing_rows': 0, 'bridge_calls': 0,
                 'generated_rows': 0, 'unavailable_generated_rows': 0,
                 'partial_generated_rows': 0,
                 'unknown_clocks': 0, 'missing_source_rows': 0}
        for index, row in enumerate(trace['rows']):
            assert len(row['ports']) == 32 and row['bridge_calls'] in [0, 1]
            stats['bridge_calls'] += row['bridge_calls']
            generated_call = role == 'source' and row['generated_calls'] == 1
            available = sum(port['present'] for port in row['ports'])
            active = generated_call and available > 0 if role == 'source' else row['after'] > row['before']
            if generated_call:
                stats['generated_rows'] += 1
                stats['unavailable_generated_rows'] += available == 0
                stats['partial_generated_rows'] += 0 < available < 32
            stats['active_rows' if active else 'nonadvancing_rows'] += 1
            if not row['clock_known']:
                stats['unknown_clocks'] += 1
                problems.append(f'{role} row{index}: unknown clock')
            assert row['generated_calls'] in [0, 1]
            if (active or generated_call) and (row['buffer_calls'] != trace['expected_buffer_calls'] or
                           not 0 < row['duration'] <= 65536):
                problems.append(f'{role} row{index}: incomplete active buffer coverage')
            if role == 'owner' and active and row['bridge_calls'] != 1:
                problems.append(f'owner row{index}: unobserved advancing bridge')
            for channel, port in enumerate(row['ports']):
                assert port['channel'] == channel and len(port['bits']) == 8
                stats['observed_ports'] += port['seen']
                stats['sampled_ports'] += port['sampled']
                if generated_call and not port['seen']:
                    problems.append(f'source row{index} channel{channel}: missing buffer observation')
                if generated_call and not port['present']:
                    # Source runs before/after links exist. Keep absent buffers visible;
                    # any recorder consumption of such a row is refused below.
                    if port['sampled']:
                        problems.append(f'source row{index} channel{channel}: sampled absent buffer')
                    continue
                if not active:
                    continue  # Retain nonadmitted rows; no fabricated engine work.
                if not (port['seen'] and port['present'] and port['sampled'] and
                        port['frames'] == row['duration']):
                    problems.append(f'{role} row{index} channel{channel}: incomplete markers')
                    continue
                expected = markers(row['position'], row['duration'], channel)
                actual = port['bits'][:min(row['duration'], 4)] + port['bits'][4:4 + min(row['duration'], 4)]
                if actual != expected:
                    shifts = [shift for shift in [-row['duration'], row['duration']]
                              if row['position'] + shift >= 0 and
                              actual == markers(row['position'] + shift, row['duration'], channel)]
                    mismatches.append({'role': role, 'row': index, 'channel': channel,
                                       'clock': key(row), 'buffer_identity': port['buffer_identity'],
                                       'actual_bits': actual, 'expected_bits': expected,
                                       'matching_diagnostic_shifts': shifts})
            if generated_call:
                assert key(row) not in generated
                generated[key(row)] = row
            elif role == 'owner' and active:
                if key(row) not in generated:
                    stats['missing_source_rows'] += 1
                    problems.append(f'owner row{index}: no generated source at same clock')
                elif not all(p['seen'] and p['present'] and p['sampled']
                             for p in generated[key(row)]['ports']):
                    problems.append(f'owner row{index}: consumed unavailable source buffer')
                elif any(a['bits'] != b['bits'] for a, b in
                         zip(row['ports'], generated[key(row)]['ports'])):
                    problems.append(f'owner row{index}: source/received markers differ')
        assert stats['active_rows'] > 0
        coverage[role] = stats
    assert stages['test_only_linker_wrappers'] and stages['nested_costs_inclusive']
    if stages['total_bridge']['calls'] != coverage['owner']['bridge_calls']:
        problems.append('Manual bridge count differs from independent port observer')
    for name, cost in [('bridge', stages['total_bridge']), *stages['total_stages'].items()]:
        assert cost['cpu_ns'] <= cost['wall_ns']
        if cost['unknown_intervals']:
            problems.append(name + ': unknown stage intervals')
    owner_clocks = {key(row) for row in owner['rows'] if row['bridge_calls']}
    for row in stages['worst_bridge_callbacks']:
        assert tuple(row[k] for k in ['clock_id', 'clock_cycle', 'clock_position', 'quantum']) in owner_clocks
        for cost in row['stages'].values():
            if cost['calls']:
                assert cost['maximum_call_known']
                assert cost['maximum_call_ordinal'] < cost['calls']
                assert cost['maximum_call_cpu_ns'] <= cost['maximum_call_wall_ns'] <= cost['wall_ns']
    return {'qualified_trace': not problems and not mismatches, 'coverage': coverage,
            'problems': problems, 'marker_mismatches': mismatches,
            'processing_stages': stages,
            'files': {name: {'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                             'bytes': p.stat().st_size} for name, p in files.items()}}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    receipt = json.loads(args.receipt.read_text())
    result = analyze(Path(receipt['project_directory']))
    result['fixture_qualified'] = 'error' not in receipt and receipt.get('exit_code') == 0
    result['fixture_receipt_sha256'] = hashlib.sha256(args.receipt.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ['qualified_trace', 'fixture_qualified', 'coverage']}))
    if not result['qualified_trace'] or not result['fixture_qualified']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
