#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent retained SDK lease/raw-source analysis; never opens audio."""
from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
from verify_input_acquisition import float_wav, require
from verify_windows_desktop import path
from verify_windows_playback import coefficients


def lease_times(observation, after_release):
    ending = ('releasedTicks','callbackReturnedTicks') if after_release else ('callbackReturnedTicks','releasedTicks')
    times = [observation[k] for k in ('waitStartedTicks','wakeTicks','acquireStartedTicks','acquiredTicks')+ending]
    require(all(type(v) is int and v >= 0 for v in times) and times == sorted(times),
            'Native lease timing order/type differs')
    return times


def analyze(root):
    root = Path(root)
    t = json.loads((root/'native-leases.json').read_text())
    report = json.loads((root/'probe.json').read_text())
    require(report['format'] == 'sc-wasapi-recording-probe' and report['generatedSource'] and
            report['loopback'] and report['sampleRate'] == 48000 and report['nativeChannels'] == 2 and
            report['sourceSessionVolume'] == 1 and not report['sourceSessionMuted'],
            'Owned source/route/session differs')
    after_release = t['format'] == 'sc-wasapi-capture-lease-trace-v2'
    require(t['format'] in ('sc-wasapi-capture-lease-trace-v1','sc-wasapi-capture-lease-trace-v2') and
            (not after_release or t.get('processingAfterRelease') is True) and t['sampleRate'] == 48000 and
            t['channels'] == 2 and t['capacity'] == 2048 and t['dropped'] == 0 and
            not t['sequenceExhausted'] and 0 < t['qpcFrequency'] <= 10**12 and
            0 < t['devicePeriod100ns'] <= 10000000 and 0 <= t['streamLatency100ns'] <= 10000000 and
            0 < t['bufferFrames'] <= 32768 and 0 < len(t['leases']) <= 2048,
            'Capture trace configuration/loss differs')
    require(report['nativeLeaseTraceRetained'] and report['nativeTraceDropped'] == 0 and
            report['nativeBufferFrames'] == t['bufferFrames'] and report['callbacks'] == len(t['leases']) and
            report['cppAllocations'] == report['cppFrees'] == 0 and report['defaultsUnchanged'] and
            report['streamFailure'] is None, 'Native trace owner/audit differs')
    ns = lambda ticks: ticks*1000000000//t['qpcFrequency']
    previous = None; gaps = []; maximum_lease = maximum_callback = maximum_wait = 0
    for n,o in enumerate(t['leases']):
        lease_times(o,after_release)
        require(o['sequence'] == n and o['clockValid'] and o['released'] and o['callbackInvoked'] and
                o['acquireHresult'] == o['releaseHresult'] == 0 and 0 < o['frames'] <= t['bufferFrames'] and
                o['flags'] & ~7 == 0 and 0 <= o['batchIndex'] < 16,
                'Native lease order/identity/clock differs')
        lease = ns(o['releasedTicks']-o['acquiredTicks'])
        callback_origin = 'releasedTicks' if after_release else 'acquiredTicks'
        callback = ns(o['callbackReturnedTicks']-o[callback_origin])
        maximum_lease = max(maximum_lease,lease); maximum_callback = max(maximum_callback,callback)
        maximum_wait = max(maximum_wait,ns(o['wakeTicks']-o['waitStartedTicks']))
        if previous:
            require(o['acquireStartedTicks'] >= max(previous['releasedTicks'],previous['callbackReturnedTicks']) and
                    o['wakeSequence'] >= previous['wakeSequence'], 'Native thread order reversed')
            expected = previous['devicePosition']+previous['frames']
            if o['devicePosition'] != expected or o['flags'] & 1:
                gaps.append({'sequence':n,'expectedPosition':expected,'sdkPosition':o['devicePosition'],
                             'positionDifferenceFrames':o['devicePosition']-expected,'sdkFlags':o['flags'],
                             'previousLeaseNs':ns(previous['releasedTicks']-previous['acquiredTicks']),
                             'previousCallbackNs':ns(previous['callbackReturnedTicks']-previous[callback_origin]),
                             'sincePreviousReleaseNs':ns(o['acquireStartedTicks']-previous['releasedTicks'])})
        previous = o
    raw_path = path(root,report['assetPath']); raw = float_wav(raw_path)
    journal = json.loads((raw_path.parent/'journal.json').read_text())
    require(len(raw) == journal['committedFrames'] == report['frames'] and
            hashlib.sha256(raw_path.read_bytes()).hexdigest() == report['assetSha256'] and
            journal['phase'] == 'finalized' and 0 < len(raw) <= 480000 and
            all(math.isfinite(v) for v in raw), 'Raw media/journal identity differs')
    source = array('f'); source.frombytes((root/'generated-source.f32').read_bytes())
    if sys.byteorder != 'little': source.byteswap()
    require(len(source) == 576000*2, 'Fixture source extent differs')
    seed = 0x753bdce1; amplitude = struct.unpack('<f',struct.pack('<f',.1))[0]
    for n in range(576000):
        for c in range(2):
            seed = (seed*1664525+1013904223) & 0xffffffff
            expected = 0. if n < 2048 or 48000 <= n < 49024 else \
                struct.unpack('<f',struct.pack('<f',((seed>>8)/16777216.-.5)*amplitude))[0]
            require(source[n*2+c] == expected, 'Original fixture generator differs')
    source = source[::2]
    marker = next((n for n,v in enumerate(raw) if abs(v) > .0001),len(raw))
    require(marker < len(raw), 'Raw source marker missing')
    needle = raw[marker:marker+32]
    offsets = [n-marker for n in range(2048,96000) if source[n:n+32] == needle]
    require(len(offsets) == 1, 'Ambiguous raw source alignment')
    offset = offsets[0]; begin = max(0,-offset)
    require(all(v == 0 for v in raw[:begin]) and raw[begin:] == source[begin+offset:len(raw)+offset],
            'Raw source prefix lost/repeated/altered')
    fault_path = raw_path.parent/'first-fault.json'
    qualified = report['workflowAccepted']
    first = t['leases'][0]
    require(report['firstPacket']['devicePosition'] == first['devicePosition'] and
            report['firstPacket']['frames'] == first['frames'] and
            report['firstPacket']['flags'] == first['flags'] and
            report['firstPacket']['qpc100ns'] == first['packetQpc100ns'],
            'Initial SDK packet identity differs')
    export_error = None
    if qualified:
        require(not gaps and report['status'] == 2 and report['endReason'] == 2 and
                report['frames'] == 480000 and sum(o['frames'] for o in t['leases']) == len(raw) and
                not report['firstBridgeFault'] and not fault_path.exists() and report['reopenedAndExported'],
                'Complete capture claim differs')
        session = json.loads((root/'project.json').read_text())
        bands = session['tracks'][0]['processors'][0]['bands']
        require(bands[0]['gainDb'] == -6 and all(b['gainDb'] == 0 for b in bands[1:]), 'Fixture EQ state differs')
        b0,b1,b2,a1,a2 = coefficients(bands[0],-6); z1 = z2 = 0.; expected = array('f')
        for x in raw:
            y = b0*x+z1; z1,z2 = b1*x-a1*y+z2,b2*x-a2*y; expected.append(y)
        export_path = root/'exports/native.wav'; exported = float_wav(export_path)
        require(len(exported) == len(expected) and all(math.isfinite(v) for v in exported) and
                hashlib.sha256(export_path.read_bytes()).hexdigest() == report['exportSha256'], 'Export extent/hash differs')
        export_error = max(abs(a-b) for a,b in zip(expected,exported))
        require(export_error <= 1e-7, 'Reopened in-process EQ export differs')
    else:
        f = json.loads(fault_path.read_text())['fault']
        require(gaps and report['firstBridgeFault'] and not report['reopenedAndExported'] and
                report['endReason'] == f['status'] == report['status'] == 6 and f['reason'] == 5 and
                f['capturedFrames'] == len(raw) == sum(o['frames'] for o in t['leases'][:gaps[0]['sequence']]) and
                gaps[0]['sdkPosition'] == f['rejected']['position'] and gaps[0]['sdkFlags'] & 1 and
                f['rejected']['discontinuity'] and
                f['rejected']['duration'] == t['leases'][gaps[0]['sequence']]['frames'] and
                gaps[0]['expectedPosition'] == f['previous']['position']+f['previous']['duration'],
                'Native SDK gap/bridge first fault differs')
    result = {'format':'sc-native-capture-lease-analysis-v1','rawFrames':len(raw),'rawSourceOffset':offset,
            'rawMaximumError':0,'exportMaximumError':export_error,'sdkGaps':gaps,
            'maximumLeaseNs':maximum_lease,'maximumCallbackNs':maximum_callback,'maximumWaitNs':maximum_wait,
            'leasesExceedingDevicePeriod':sum(ns(o['releasedTicks']-o['acquiredTicks']) > t['devicePeriod100ns']*100
                                             for o in t['leases']),
            'ownedCaptureWorkflowQualified':qualified,'nativeAudioReplayed':False,
            'driverOrSchedulingCauseIsolated':False,'installedWorkflowQualified':False}
    if after_release: result['processingAfterRelease'] = True
    return result


if __name__ == '__main__':
    require(len(sys.argv) == 2,'Supply retained native capture trace directory')
    print(json.dumps(analyze(Path(sys.argv[1])),indent=2))
