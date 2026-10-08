#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ensure corrupted media and misleading qualifications cannot hide capture failures."""
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from verify_windows_capture_trace import analyze, claims, executions, unpack
from analyze_windows_capture_trace import lease_times


def refused(call):
    try: call()
    except ValueError: return
    raise AssertionError('Misleading capture evidence accepted')


def run():
    # Synthetic timestamp model only: never relabel original v1 native evidence.
    ticks = dict(waitStartedTicks=100,wakeTicks=110,acquireStartedTicks=120,
                 acquiredTicks=130,releasedTicks=140,callbackReturnedTicks=200)
    assert lease_times(ticks,True) == [100,110,120,130,140,200]
    refused(lambda: lease_times(ticks,False))
    changed = dict(ticks,callbackReturnedTicks=135)
    refused(lambda: lease_times(changed,True))
    assert lease_times(changed,False) == [100,110,120,130,135,140]
    refused(lambda: lease_times(dict(ticks,releasedTicks=140.0),True))
    print('Synthetic v1/v2 timing model: valid controls and three order/type refusals passed')
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-capture-trace.json').read_text())
    claims(receipt); count = 0
    for key in ('installedWorkflowQualified','physicalAudioQualified','sustainedAudioQualified',
                'driverOrSchedulingCauseIsolated','cableComparisonQualified','fullSuiteParity',
                'previewRebuilt','publicBinaryUploaded','refusedTaskUnregistered'):
        changed = deepcopy(receipt); changed[key] = True
        refused(lambda: claims(changed)); count += 1
    with tempfile.TemporaryDirectory(prefix='sc-trace-refusal-') as temp:
        root = Path(temp); capsule = receipt['capsule']
        unpack(ROOT/'tests/results/X007'/capsule['name'],capsule['files'],root)
        read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
        results = [read(root/'native'/f'capture-trace-v{i}'/'result.json') for i in (1,2,3)]
        executions(*results)
        for target,key,value in ((1,'exitCode',0),(2,'exitCode',0)):
            changed = deepcopy(results); changed[1]['tests'][target+2][key] = value
            refused(lambda: executions(*changed)); count += 1
        changed = deepcopy(results); changed[2]['tests'][0]['exitCode'] = 0
        refused(lambda: executions(*changed)); count += 1
        project = root/'native/capture-trace-v2/trace-2'
        analyze(project)
        trace = project/'native-leases.json'; original = trace.read_bytes()
        for mutate in (
            lambda t: t.update(dropped=1),
            lambda t: t['leases'][2].update(sequence=3),
            lambda t: t['leases'][2].update(releasedTicks=t['leases'][2]['acquiredTicks']-1),
        ):
            changed = read(trace); mutate(changed); trace.write_text(json.dumps(changed))
            refused(lambda: analyze(project)); trace.write_bytes(original); count += 1
        probe = project/'probe.json'; original_probe = probe.read_bytes(); report = read(probe)
        raw = project/report['assetPath']; original_raw = raw.read_bytes(); changed_raw = bytearray(original_raw)
        offset = 12
        while changed_raw[offset:offset+4] != b'data':
            size = struct.unpack_from('<I',changed_raw,offset+4)[0]
            offset += 8+size+(size & 1)
            assert offset+8 <= len(changed_raw)
        sample = offset+8+10000*4
        struct.pack_into('<f',changed_raw,sample,struct.unpack_from('<f',changed_raw,sample)[0]*.5)
        raw.write_bytes(changed_raw)
        report['assetSha256'] = hashlib.sha256(changed_raw).hexdigest()
        probe.write_text(json.dumps(report))
        refused(lambda: analyze(project)); count += 1
        raw.write_bytes(original_raw); probe.write_bytes(original_probe)
        fault = raw.parent/'first-fault.json'; original_fault = fault.read_bytes()
        changed = read(fault); changed['fault']['previous']['duration'] -= 1
        fault.write_text(json.dumps(changed)); refused(lambda: analyze(project)); count += 1
        fault.write_bytes(original_fault)
    print(f'Native capture trace: original failure control and {count} misleading/corrupt-evidence refusals passed')


if __name__ == '__main__': run()
