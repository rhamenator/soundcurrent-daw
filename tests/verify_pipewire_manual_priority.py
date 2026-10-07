#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Require actual priority callback completion while native disk startup is held."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_pipewire_manual_fault import qualify
from verify_pipewire_manual_trace import analyze


def priority(result):
    assert result['native'] and result['mode'] in ['early-stop', 'early-cancel']
    assert result['priority_disk_startup_held'] and result['priority_callback_applied']
    assert result['priority_held_at_request'] is True
    assert result['priority_service_delay_frames'] == 1536
    assert result['first_service_frame'] >= result['punch_in'] + result['priority_service_delay_frames']
    assert result['priority_requested_cancel'] == (result['mode'] == 'early-cancel')
    assert result['priority_request_before'] <= result['priority_request_after'] <= result['stopped_position']
    assert result['priority_request_after'] >= result['punch_in'] + 1000
    assert result['stopped_position'] - result['priority_request_after'] <= result['callback_timing']['owner']['maximum_quantum']
    qualify(result, True, result['mode'])  # Full original waveform/recovery/timing gates.


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receipt', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    r = json.loads(args.receipt.read_text())
    assert r['exit_code'] == 0 and 'error' not in r
    priority(r['child_result'])
    trace = analyze(Path(r['project_directory']))
    assert trace['qualified_trace']
    result = {'priority_qualified': True, 'original_fixture_gates_met': True,
              'trace': trace, 'receipt_sha256': hashlib.sha256(args.receipt.read_bytes()).hexdigest()}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'priority_qualified': True, 'mode': r['mode'], 'coverage': trace['coverage']}))


if __name__ == '__main__':
    main()
