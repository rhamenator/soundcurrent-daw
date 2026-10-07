#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse mutations of an original production readiness trace; no audio replay."""
import argparse
import copy
import json
from pathlib import Path
from verify_native_buffer_startup import load, verify


def assign(root, path, value):
    for key in path[:-1]:
        root = root[key]
    root[path[-1]] = value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--project', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    inputs, _ = load(args.receipt, args.project)
    verify(*inputs)
    gate, h, markers, trace, child = inputs
    owner = next(n for n, f in enumerate(h['filters']) if 'recording-manual' in f['name'])
    source = next(n for n, f in enumerate(h['filters']) if 'manual-fault-source-' in f['name'])
    row = next(n for n, r in enumerate(h['filters'][owner]['rows']) if r['queries'][0]['returned'])
    output_row = next(n for n, r in enumerate(h['filters'][owner]['rows']) if r['queries'][32]['returned'])
    gate_row = next(n for n, r in enumerate(h['filters'][source]['rows'])
                    if all(r[k] == v for k, v in gate['clock'].items()))
    query = [1, 'filters', owner, 'rows', row, 'queries', 0]
    output = [1, 'filters', owner, 'rows', output_row, 'queries', 32]
    held = [1, 'filters', source, 'rows', gate_row, 'queries', 23]
    cases = [(f'gate {k}', [0, k], v) for k, v in [
        ('policy', 'defer-unready'), ('test_only', False), ('selected_channel', 22),
        ('entered', False), ('entered_outside_rt', False), ('completed', False),
        ('released', False), ('timed_out', True), ('api_suppressed', True),
        ('io_known', True), ('live_buffers', 0), ('returned', True), ('known_buffer', True)]]
    cases += [('gate clock ' + k, [0, 'clock', k], gate['clock'][k] + 1)
              for k in gate['clock']]
    cases += [('legacy acquisition version', [1, 'acquisition_version'], 1),
              ('legacy marker version', [2, 'owner', 'acquisition_version'], 1)]
    cases += [('ownership ' + k, query + [k], v) for k, v in [
        ('sdk_dequeues', 0), ('sdk_queues', 0), ('native_returned', False),
        ('native_known', False), ('native_buffer', 4294967295), ('queue_matched', False),
        ('queue_result', -5), ('acquisition_status', 2), ('io_known', False),
        ('io_status', 0), ('io_buffer', 4294967295), ('chunk_stride', 8),
        ('chunk_flags', 1), ('extent_bytes', 1), ('chunk_offset', 1),
        ('chunk_bytes', 1), ('data_flags', 0), ('port', 1), ('api_suppressed', True)]]
    cases += [('output ' + k, output + [k], v) for k, v in [
        ('io_status', 2), ('chunk_offset', 4), ('chunk_bytes', 1), ('data_flags', 0)]]
    cases += [('held port ' + k, held + [k], v) for k, v in [
        ('sdk_dequeues', 1), ('sdk_queues', 1), ('api_suppressed', True),
        ('live_buffers', 0), ('acquisition_status', 1)]]
    cases += [('marker aggregate ' + k, [2, 'owner', 'rows', row, k], 9999)
              for k in ['sdk_dequeues', 'sdk_queues', 'sdk_returned', 'sdk_queue_failures']]
    cases += [('missing channel ordinal', [2, 'owner', 'rows', row, 'ports', 0, 'channel'], 7),
              ('unseen channel', [2, 'owner', 'rows', row, 'ports', 0, 'seen'], False),
              ('child timing gate', [4, 'callback_timing', 'owner', 'finite_deadline_thresholds_met'], False),
              ('child full raw extent', [4, 'raw_samples_verified'], 1),
              ('marker full waveform', [3, 'qualified_trace'], False)]
    refused = []
    for name, path, value in cases:
        altered = copy.deepcopy(inputs)
        assign(altered, path, value)
        try:
            verify(*altered)
        except (AssertionError, KeyError, ValueError):
            refused.append(name)
        else:
            raise AssertionError('Mutation accepted: ' + name)
    altered = copy.deepcopy(inputs)
    del altered[0]['clock']['position']
    try:
        verify(*altered)
    except (AssertionError, KeyError, ValueError):
        refused.append('missing gate clock domain term')
    else:
        raise AssertionError('Missing gate clock term accepted')
    args.output.write_text(json.dumps({'original_native_baseline_verified': True,
                                      'audio_replayed': False,
                                      'mutations_refused': refused}, indent=2) + '\n')
    print(json.dumps({'mutations_refused': len(refused)}))


if __name__ == '__main__':
    main()
