#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic trace/observer controls, never native Windows qualification."""
from array import array
from pathlib import Path
import copy
import hashlib
import json
import tempfile
from analyze_windows_render_trace import analyze_trace, compare_observer


with tempfile.TemporaryDirectory(prefix='sc-render-trace-') as directory:
    root = Path(directory); bank = root/'render-lease-samples.f32'
    expected = array('f'); seed = 0x1a2b3c4d
    for n in range(24000):
        seed = (seed*1664525+1013904223)&0xffffffff
        expected.append(((seed>>8)/16777216.-.5)*.1)
    stereo = array('f')
    for value in expected: stereo.extend((value,0.))
    rows = []
    for sequence,start in enumerate(range(0,len(expected),2048)):
        frames = min(2048,len(expected)-start)
        rows.append({'sequence':sequence,'sampleOffsetValues':start*2,'requestedFrames':frames,
                     'copiedFrames':frames,'releasedFrames':frames,'action':1 if start+frames==len(expected) else 0,
                     'acquired':True,'acquireHresult':0,'releaseObserved':True,'releaseHresult':0,
                     'samplesComplete':True,'submittedFrames':480+start,'contentSubmittedFrames':start,
                     'startupFrames':480,'clockPosition':start,'clockFrequency':48000,'qpc100ns':start,
                     'paddingFrames':0})
    template = {'format':'sc-wasapi-render-trace-v1','channels':2,'sampleRate':48000,'maximumFrames':2048,
                'contentLeasesOnly':True,'joined':True,'malformed':False,'lostRows':0,'lostSampleFrames':0,
                'samplePath':bank.name,'sampleValues':len(stereo),'observations':rows}
    def write(report, values=stereo):
        bank.write_bytes(values.tobytes()); report['sampleValues']=len(values)
        report['sampleSha256']=hashlib.sha256(bank.read_bytes()).hexdigest()
        (root/'render-trace.json').write_text(json.dumps(report))
    write(copy.deepcopy(template)); result,committed = analyze_trace(root,expected)
    assert result['leaseReferenceFidelity'] and committed==expected and result['committedContentFrames']==24000
    assert result['nativeEndpointQualified'] is False
    refused = 0
    for mutation in range(11):
        report = copy.deepcopy(template); write(report)
        if mutation==0: report['lostRows']=1
        elif mutation==1: report['lostSampleFrames']=1
        elif mutation==2: report['joined']=False
        elif mutation==3: report['malformed']=True
        elif mutation==4: report['observations'][1]['sequence']=2
        elif mutation==5: report['observations'][1]['sampleOffsetValues']+=2
        elif mutation==6: report['observations'][1]['releaseObserved']=False
        elif mutation==7: report['observations'][1]['copiedFrames']-=1
        elif mutation==8: report['samplePath']='../outside.f32'
        elif mutation==9: report['observations'][1]['contentSubmittedFrames']+=1
        else: report['observations'][1]['clockFrequency']=0
        (root/'render-trace.json').write_text(json.dumps(report))
        try: analyze_trace(root,expected)
        except (AssertionError,ValueError): refused+=1
        else: raise AssertionError('Mutated trace accepted: '+str(mutation))
    assert refused==11
    # A changed sample bank with an updated hash remains altered, never repaired.
    poisoned = array('f',stereo); poisoned[-1000]*=.8
    write(copy.deepcopy(template),poisoned)
    result,_ = analyze_trace(root,expected); assert not result['leaseReferenceFidelity']
    # Failed ReleaseBuffer retains copied samples as rejected; only prior leases commit.
    report=copy.deepcopy(template); report['observations'][-1]['releaseHresult']=-2147467259
    write(report); result,committed=analyze_trace(root,expected)
    assert result['releaseFailures']==1 and result['leaseReferenceFidelity'] and len(committed)==22528
    # An unused Abort retains no unwritten SDK backing and commits no samples.
    report=copy.deepcopy(template); last=report['observations'][-1]
    last.update({'action':2,'copiedFrames':0,'releasedFrames':0,'samplesComplete':False})
    write(report,stereo[:22528*2]); result,committed=analyze_trace(root,expected)
    assert result['aborts']==1 and len(committed)==22528
    # Valid failed acquisition records no release or samples.
    last.update({'acquired':False,'acquireHresult':-2147024891,'releaseObserved':False})
    write(report,stereo[:22528*2]); result,_=analyze_trace(root,expected)
    assert result['acquireFailures']==1 and result['releaseFailures']==0
    left=array('f',[0.]*100); left.extend(expected); right=array('f',[0.]*len(left))
    result=compare_observer(expected,left,right)
    assert result['observerComparedLeaseFidelity'] and result['fullCommittedExtentObserved']
    fade=array('f',left)
    for n in range(480): fade[-480+n]*=(480-n)/480
    assert not compare_observer(expected,fade,right)['observerComparedLeaseFidelity']
    scaled=array('f',(v*.5 for v in left))
    assert not compare_observer(expected,scaled,right)['observerComparedLeaseFidelity']
    missing=compare_observer(expected,left[:-100],right[:-100])
    assert missing['observerComparedLeaseFidelity'] and not missing['fullCommittedExtentObserved']
    assert missing['unobservedCommittedFrames']==100
print('Render trace analyzer: sample alteration, metadata refusals, failed release/acquire, abort, unity Stop tail and missing extent passed (synthetic only)')
