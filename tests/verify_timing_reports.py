#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline check of opt-in owned native timing reports; never selects a device."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--debug', type=Path, required=True)
parser.add_argument('--release', type=Path, required=True)
parser.add_argument('--injected', type=Path, required=True)
args = parser.parse_args()


def timing(value):
    assert value['samples'] == value['calls'] > 0
    assert value['dropped_samples'] == value['clock_failures'] == 0
    assert 0 <= value['p50_ns'] <= value['p95_ns'] <= value['p99_ns'] <= value['maximum_ns']


summaries = {}
for name, path in [('debug', args.debug), ('release', args.release)]:
    values = json.loads(path.read_text())
    assert [v['mode'] for v in values] == (['mix'] if name == 'debug' else ['mix', 'mix-disconnect', 'export'])
    summaries[name] = []
    for value in values:
        assert value['live_offline_difference'] == value['missing_frames'] == 0
        assert value['rt_allocations'] == value['rt_frees'] == value['rt_blocking_locks'] == 0
        if value['mode'] == 'mix-disconnect':
            assert 24000 <= value['frames'] < 480000
        else:
            assert value['frames'] == 480000
        assert value['tracks'] == (1 if value['mode'] == 'export' else 32)
        measured = value['callback_timing']
        timing(measured['player'])
        timing(measured['sink'])
        assert measured['unavailable_after_start'] == 0
        assert measured['minimum_quantum_frames'] == measured['maximum_quantum_frames'] > 0
        routing = value['routing_observations']
        assert routing['default_metadata_unchanged'] and routing['owned_nodes_and_links_removed']
        period = measured['minimum_quantum_frames'] * 1000000000 / 48000
        summaries[name].append({'mode': value['mode'], 'period_ns': period,
                                'player': measured['player'],
                                'maximum_to_period_ratio': measured['player']['maximum_ns'] / period})

failure = json.loads(args.injected.read_text())['diagnostics']
assert failure['mode'] == 'mix-gap' and failure['exit_code'] == 1
assert 'timed out' not in failure['stderr']
diagnostic = json.loads(next(line.split('diagnostic ', 1)[1]
    for line in failure['stderr'].splitlines() if line.startswith('Native callback timing diagnostic ')))
assert diagnostic['injected_sink_gap'] and diagnostic['unavailable_after_start'] == 0
assert diagnostic['gap_observed'] - diagnostic['gap_expected'] == diagnostic['minimum_quantum_frames']
timing(diagnostic['player'])
timing(diagnostic['sink'])
cleanup = failure['cleanup_observation']
assert cleanup['default_metadata_unchanged'] and cleanup['owned_nodes_remaining'] == cleanup['owned_links_remaining'] == 0
assert failure['scheduler_samples']
print(json.dumps({'measured_runs': summaries, 'controlled_gap_rejected_promptly': True,
                  'controlled_gap_cleanup_verified': True,
                  'historical_failure_cause_resolved': False,
                  'long_duration_or_hardware_deadline_qualification': False}, indent=2))
