#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse altered priority evidence against a retained qualified native receipt."""
import argparse
import copy
import json
from pathlib import Path

from verify_pipewire_manual_priority import priority


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    receipt = json.loads(args.receipt.read_text())
    assert receipt['exit_code'] == 0 and 'error' not in receipt
    original = receipt['child_result']
    priority(original)
    mutations = []
    for field in ['priority_disk_startup_held', 'priority_callback_applied', 'priority_held_at_request']:
        mutations.append((field + '-false', lambda r, f=field: r.update({f: False})))
        mutations.append((field + '-missing', lambda r, f=field: r.pop(f)))
    mutations.extend([
        ('wrong-cancel', lambda r: r.update(priority_requested_cancel=not r['priority_requested_cancel'])),
        ('request-reversed', lambda r: r.update(priority_request_before=r['priority_request_after'] + 1)),
        ('stop-before-request', lambda r: r.update(stopped_position=r['priority_request_after'] - 1)),
        ('request-before-target', lambda r: r.update(priority_request_before=r['punch_in'], priority_request_after=r['punch_in'])),
        ('stop-beyond-quantum', lambda r: r.update(stopped_position=r['priority_request_after'] + r['callback_timing']['owner']['maximum_quantum'] + 1)),
        ('missing-request-clock', lambda r: r.pop('priority_request_after')),
        ('service-before-delay', lambda r: r.update(first_service_frame=r['punch_in'])),
        ('synthetic-as-native', lambda r: r.update(native=False)),
        ('unrelated-mode', lambda r: r.update(mode='hash-fail')),
        ('raw-oracle-failed', lambda r: r.update(maximum_sample_difference=1)),
        ('callback-lock', lambda r: r.update(rt_blocking_locks=1)),
        ('cycle-overrun', lambda r: r['callback_timing']['owner'].update(callback_end_after_cycle_period_count=1)),
    ])
    refused = []
    for name, alter in mutations:
        candidate = copy.deepcopy(original)
        alter(candidate)
        try:
            priority(candidate)
        except (AssertionError, KeyError, TypeError):
            refused.append(name)
        else:
            raise AssertionError('Altered priority evidence accepted: ' + name)
    result = {'mode': original['mode'], 'qualified_original': True,
              'altered_refusals': refused, 'count': len(refused),
              'native_rerun': False}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
