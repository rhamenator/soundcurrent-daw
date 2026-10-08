#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic Stop/end oracle cases; these never qualify a native endpoint."""
from array import array
from pathlib import Path
import hashlib
import json
import struct
import tempfile
from analyze_windows_stop import analyze


def make(root, cancel=False, fade=False, missing=0, guard=0, source_range=96000, cancel_guard=False):
    f32 = lambda v: array('f', [v])[0]
    source = array('f'); seed = 0x1a2b3c4d
    for n in range(96000):
        seed = (seed*1664525+1013904223)&0xffffffff
        v = 0. if cancel and n >= 84000 else f32(f32((seed>>8)/16777216.-.5)*f32(.1))
        source.extend((f32(.04) if n == 0 else v, 0.))
    content = 48000 if cancel else 96000 if source_range == 96000 else 2048
    end = (43616 if cancel else source_range)-missing
    (root/'source-stereo.f32').write_bytes(source.tobytes())
    copied = min(content,source_range)
    (root/'submitted-stereo.f32').write_bytes(source[:copied*2].tobytes()+bytes((96000-copied)*8))
    wet = array('f', [0.]*(544*2))
    for n in range(end):
        wet.extend((source[n*2]*((end-n)/480 if fade and n >= end-480 else 1), 0.))
    committed_guard = 0 if cancel else guard
    if not cancel_guard: wet.extend([0.]*(committed_guard*2))
    payload = wet.tobytes(); fmt = struct.pack('<HHIIHH',3,2,48000,384000,8,32)
    body = b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
    capture = root/'loopback/media/take.wav'; capture.parent.mkdir(parents=True)
    capture.write_bytes(b'RIFF'+struct.pack('<I',len(body))+body)
    (capture.parent/'journal.json').write_text(json.dumps({'phase':'finalized','committedFrames':len(wet)//2,
        'rejectedFrames':0,'observedInvalidInputSamples':0,'timingDomain':'engine-frames',
        'timingOrigin':{'backend':4,'rateNumerator':1,'rateDenominator':48000}}))
    rows = [{'submittedFrames':n+480,'contentSubmittedFrames':n,'startupFrames':480,
             'frames':min(2048,content-n),'clockFrequency':48000,'clockPosition':n,
             'qpc100ns':n,'paddingFrames':0} for n in range(0,content,2048)]
    report = {'format':'sc-wasapi-direct-startup-probe','nativeSdkAccepted':True,'sourceFrames':96000,
              'rate':48000,'silentLeadFrames':0,'drained':not (cancel or cancel_guard),'cppAllocations':0,'cppFrees':0,
              'defaultsUnchanged':True,'mixerOrEqInvoked':False,'physicalOrSustainedTimingQualified':False,
              'bufferFrames':4800,'submittedFrames':content+480+committed_guard,'observations':rows,
              'contentSubmittedFrames':content,'sourceCursor':content,'capturePath':'media/take.wav',
              'captureSha256':hashlib.sha256(capture.read_bytes()).hexdigest(),'captureFrames':len(wet)//2,
              'sourceKind':'noise','mode':'cancel-active' if cancel else 'cancel-guard' if cancel_guard else 'non-silent-end',
              'stopRequestedAtMinimumContentFrame':48000 if cancel else 0,
              'startupFrames':480,'devicePeriod100ns':100000,'streamLatency100ns':0}
    if guard or source_range != 96000:
        report.update({'endGuardFrames':guard,'endGuardSubmittedFrames':committed_guard,'sourceRangeFrames':source_range})
    (root/'probe.json').write_text(json.dumps(report))
    return report, capture


with tempfile.TemporaryDirectory(prefix='sc-stop-analysis-') as directory:
    parent = Path(directory)
    for name, cancel, fade, missing in [('complete',False,False,0),('truncated',False,False,64),
                                        ('cancel-prefix',True,False,0),('cancel-altered',True,True,0)]:
        root = parent/name; root.mkdir(); make(root,cancel,fade,missing)
        r = analyze(root)
        assert r['fixtureOffsetFrames'] == 544 and r['directPathAlterationObserved'] == fade
        assert r['fullNonSilentEndQualified'] == (not cancel and not fade and not missing)
        assert r['capturedPrefixFidelityQualified'] is (not fade)
        assert r['beforeFinal960MaximumResidual'] == 0
        if fade: assert r['lastAffectedFrame'] == r['matchedFrames']-1
        if missing: assert r['unobservedSubmittedSourceFrames'] == missing
    root = parent/'refusals'; root.mkdir(); report,capture = make(root,True)
    for change in ('nonfinite-capture','changed-lease','wrong-clock-coordinate','wrong-source-cursor'):
        files = {p:p.read_bytes() for p in (capture,root/'probe.json',root/'submitted-stereo.f32')}
        if change == 'nonfinite-capture':
            data = bytearray(capture.read_bytes()); struct.pack_into('<f',data,44+40000*8,float('nan'))
            capture.write_bytes(data); report['captureSha256'] = hashlib.sha256(data).hexdigest()
        elif change == 'changed-lease':
            data = bytearray((root/'submitted-stereo.f32').read_bytes()); struct.pack_into('<f',data,0,.03)
            (root/'submitted-stereo.f32').write_bytes(data)
        elif change == 'wrong-clock-coordinate': report['observations'][0]['contentSubmittedFrames'] = 480
        else: report['sourceCursor'] += 1
        (root/'probe.json').write_text(json.dumps(report))
        try: analyze(root)
        except ValueError: pass
        else: raise AssertionError('Accepted '+change)
        for p,data in files.items(): p.write_bytes(data)
        report = json.loads((root/'probe.json').read_text())
    for name, cancel, source_range in [('guarded-end',False,96000),('guarded-cancel',True,96000),
                                      ('one-frame',False,1),('short-range',False,31)]:
        root = parent/name; root.mkdir(); make(root,cancel,guard=480,source_range=source_range)
        r = analyze(root)
        assert r['fullNonSilentEndQualified'] is (not cancel)
        assert r['endGuardSubmittedFrames'] == (0 if cancel else 480)
        assert r['sourceRangeFrames'] == source_range
    root = parent/'guard-refusals'; root.mkdir(); report,capture = make(root,guard=480)
    for field, value in [('endGuardSubmittedFrames',960),('sourceCursor',96480)]:
        original = report[field]; report[field] = value
        (root/'probe.json').write_text(json.dumps(report))
        try: analyze(root)
        except ValueError: pass
        else: raise AssertionError('Accepted guard as DSP or duplicate guard')
        report[field] = original
    root = parent/'cancel-during-guard'; root.mkdir()
    report,capture = make(root,missing=4480,guard=480,cancel_guard=True)
    (root/'probe.json').write_text(json.dumps(report))
    r = analyze(root)
    assert not r['fullNonSilentEndQualified'] and r['capturedPrefixFidelityQualified']
    assert r['unobservedSubmittedSourceFrames'] == 4480 and r['endGuardSubmittedFrames'] == 480
    report['drained'] = True; (root/'probe.json').write_text(json.dumps(report))
    try: analyze(root)
    except ValueError: pass
    else: raise AssertionError('Cancellation during guard promoted to completion')
    for cancel in (False,True):
        root = parent/('scaled-cancel' if cancel else 'scaled-end'); root.mkdir()
        report,capture = make(root,cancel)
        data = bytearray(capture.read_bytes())
        for offset in range(44,len(data),8):
            value = struct.unpack_from('<f',data,offset)[0]
            struct.pack_into('<f',data,offset,value*.5)
        capture.write_bytes(data); report['captureSha256'] = hashlib.sha256(data).hexdigest()
        (root/'probe.json').write_text(json.dumps(report))
        r = analyze(root)
        assert abs(r['measuredInteriorGain']-.5) < 1e-6 and r['directPathAlterationObserved']
        assert r['maximumResidual'] > .01 and not r['capturedPrefixFidelityQualified']
        assert not r['fullNonSilentEndQualified']
print(json.dumps({'syntheticStopAnalysisCases':18,'nativeAudioReplayed':False}))
