#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic failure coverage for the read-only analyzer; no native audio claim."""
from array import array
from pathlib import Path
import hashlib
import json
import struct
import tempfile
from analyze_windows_startup import analyze


def make(root, lead=0, fade=False):
    f32 = lambda v: array('f', [v])[0]
    source = array('f'); seed = 0x1a2b3c4d
    for n in range(96000):
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        v = 0. if n < lead or n >= 84000 else f32(f32((seed >> 8)/16777216.-.5)*f32(.1))
        if n == 0 and not lead: v = f32(.04)
        source.extend((v, 0.))
    data = source.tobytes()
    (root/'source-stereo.f32').write_bytes(data); (root/'submitted-stereo.f32').write_bytes(data)
    wet = array('f', [0.]*128)
    for n in range(96000):
        wet.extend((source[n*2] * (n/480 if fade and n < 480 else 1), 0.))
    payload = wet.tobytes(); fmt = struct.pack('<HHIIHH', 3,2,48000,384000,8,32)
    wav = b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
    capture = root/'loopback/media/take.wav'; capture.parent.mkdir(parents=True)
    capture.write_bytes(b'RIFF'+struct.pack('<I',len(wav))+wav)
    (capture.parent/'journal.json').write_text(json.dumps({'phase':'finalized','committedFrames':96064,
        'rejectedFrames':0,'observedInvalidInputSamples':0,'timingDomain':'engine-frames',
        'timingOrigin':{'backend':4,'rateNumerator':1,'rateDenominator':48000}}))
    rows = []
    for n in range(0,96000,2048):
        rows.append({'submittedFrames':n,'frames':min(2048,96000-n),'clockFrequency':48000,
                     'clockPosition':n,'qpc100ns':n,'paddingFrames':0})
    report = {'format':'sc-wasapi-direct-startup-probe','nativeSdkAccepted':True,'sourceFrames':96000,
              'rate':48000,'silentLeadFrames':lead,'drained':True,'cppAllocations':0,'cppFrees':0,
              'defaultsUnchanged':True,'mixerOrEqInvoked':False,'physicalOrSustainedTimingQualified':False,
              'bufferFrames':4800,'submittedFrames':96000,'observations':rows,'capturePath':'media/take.wav',
              'captureSha256':hashlib.sha256(capture.read_bytes()).hexdigest(),'captureFrames':96064}
    (root/'probe.json').write_text(json.dumps(report))
    return report, capture


with tempfile.TemporaryDirectory(prefix='sc-startup-analysis-') as directory:
    parent = Path(directory)
    for lead, fade in ((0,False),(0,True),(12000,False)):
        root = parent/f'{lead}-{fade}'; root.mkdir(); make(root,lead,fade)
        result = analyze(root)
        assert result['fixtureOffsetFrames'] == 64 and result['directPathAlterationObserved'] == fade
        assert result['after480MaximumResidual'] == 0
        if fade: assert result['firstAffectedFrame'] == 0 and 475 <= result['lastAffectedFrame'] < 480
    root = parent/'refusals'; root.mkdir(); report,capture = make(root)
    pristine = capture.read_bytes()
    # Rehashing poisoned late media must not turn a NaN/Inf into a finite max.
    for value in (float('nan'),float('inf'),-float('inf')):
        data = bytearray(pristine); struct.pack_into('<f',data,44+80000*8,value)
        capture.write_bytes(data); report['captureSha256'] = hashlib.sha256(data).hexdigest()
        (root/'probe.json').write_text(json.dumps(report))
        try: analyze(root)
        except ValueError: pass
        else: raise AssertionError('Accepted nonfinite media')
    capture.write_bytes(pristine); report['captureSha256'] = hashlib.sha256(pristine).hexdigest()
    (root/'probe.json').write_text(json.dumps(report))
    data = bytearray((root/'submitted-stereo.f32').read_bytes()); struct.pack_into('<f',data,0,.03)
    (root/'submitted-stereo.f32').write_bytes(data)
    try: analyze(root)
    except ValueError: pass
    else: raise AssertionError('Accepted altered SDK lease')
print(json.dumps({'syntheticStartupAnalysisCases':7,'nativeAudioReplayed':False}))
