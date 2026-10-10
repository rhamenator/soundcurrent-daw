#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Post-run timing/phase diagnostics. No universal onset oracle or quality pass."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
from fractions import Fraction as F
import argparse,hashlib,json,math,struct
import numpy as np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);args=p.parse_args();root=args.folder.resolve()
processes=json.loads((root/'processes.json').read_text());rows=[];onsets=[];phases=[];partitions=[];policies=[]
events=[2048,6144,10240,14336]
gains=np.array([1,-.5,.25,-.125,.75,-.375,.1875,-.09375])
def pcm(path):
    data=path.read_bytes();assert len(data)<2*1024*1024 and data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt '
    fmt=struct.unpack_from('<HHIIHH',data,20);assert fmt[0]==3 and fmt[2]==48000 and fmt[5]==32 and fmt[1] in [2,8]
    assert len(data)==44+struct.unpack_from('<I',data,40)[0]
    a=np.frombuffer(data[44:],dtype='<f4').reshape(-1,fmt[1]);assert np.isfinite(a).all();return a
def mapped(points,x):
    if x==points[-1]['source']:return F(points[-1]['output'])
    for a,b in zip(points,points[1:]):
        if a['source']<=x<b['source']:return F(a['output'])+(x-a['source'])*F(b['output']-a['output'],b['source']-a['source'])
    raise AssertionError('Known event is outside map')
def envelope_onset(audio,ch,event):
    # Exploratory detector: 32-frame CAUSAL rectangular RMS, 10% of the
    # annotated window's maximum. Bias is measured on the original first.
    x=audio[:,ch].astype(np.float64);level=np.sqrt(np.maximum(0,np.convolve(x*x,np.ones(32)/32,mode='full')[:len(x)]))
    start=max(0,int(event)-1024);end=min(len(x),int(event)+1536);window=level[start:end];peak=float(window.max())
    if peak==0:return None
    index=int(np.flatnonzero(window>=peak*.1)[0])+start
    return {'frame':index,'window':[start,end],'thresholdRms':peak*.1,'maximumWindowRms':peak,'causalWindowFrames':32,'thresholdFraction':.1,
            'windowStartCensored':index==start,'windowEndCensored':index==end-1}
for record in processes:
    assert record['exitCode']==0
    folder=root/f"case-{record['id']:02d}";source=pcm(folder/'source.wav');rendered=pcm(folder/'rendered.wav');j=record['request'];report=record['result'];row={'id':record['id'],'request':j,'sourceSha256':record['sourceSha256'],'renderSha256':record['renderSha256'],'events':[],'phaseObservations':[]}
    if j['family']!='sustain':
        for event in events:
            target=mapped(report['points'],F(event));observations=[]
            for ch in range(j['channels']):
                raw_event=event+3*ch;s=envelope_onset(source,ch,raw_event);o=envelope_onset(rendered,ch,float(target)+3*ch)
                assert s is not None and o is not None
                raw_bias=s['frame']-raw_event
                exact_mapped_bias=mapped(report['points'],F(event+raw_bias))-target
                observation={'channel':ch,'sourceEventFrame':raw_event,'referenceTargetFrame':float(target),'sourceDetector':s,'outputDetector':o,
                             'sourceDetectorBiasFrames':raw_bias,'rawBiasCorrectedReferenceErrorFrames':o['frame']-float(target)-3*ch-raw_bias,
                             'mapBiasCorrectedReferenceErrorFrames':o['frame']-float(target)-3*ch-float(exact_mapped_bias)}
                observation['timingEstimateResolved']=not(s['windowStartCensored'] or s['windowEndCensored'] or o['windowStartCensored'] or o['windowEndCensored'])
                observations.append(observation)
            for observation in observations:
                ch=observation['channel'];observation['arrivalOffsetChangeFrames']=(observation['outputDetector']['frame']-observations[0]['outputDetector']['frame'])-(observation['sourceDetector']['frame']-observations[0]['sourceDetector']['frame'])
            row['events'].append({'rawReferenceFrame':event,'targetFrame':[target.numerator,target.denominator],'channels':observations})
            onsets.extend({'case':record['id'],'family':j['family'],'finer':j['finer'],'profile':j['profile'],'pitchMilliCents':j['pitchMilliCents'],**o} for o in observations)
    else:
        for segment,(a,b) in enumerate(zip(report['points'],report['points'][1:])):
            center=(a['output']+b['output'])//2;length=2048;start=center-length//2
            if start<0 or start+length>len(rendered):continue
            src_center=mapped([{'source':v['output'],'output':v['source']} for v in report['points']],F(center));src_start=int(src_center)-length//2
            if src_start<0 or src_start+length>len(source):continue
            taper=np.hanning(length);n=np.arange(length)
            for hz in [440.,730.]:
                carrier=np.exp(-2j*np.pi*hz*n/48000);z=np.sum(rendered[start:start+length,:].astype(np.float64)*taper[:,None]*carrier[:,None],axis=0)*np.sign(gains[:j['channels']])
                sz=np.sum(source[src_start:src_start+length,:].astype(np.float64)*taper[:,None]*carrier[:,None],axis=0)*np.sign(gains[:j['channels']])
                for ch in range(1,j['channels']):
                    expected=-2*np.pi*hz*(3*ch)/48000;wrap=lambda x:float(np.angle(np.exp(1j*x)))
                    observation={'channel':ch,'segment':segment,'sourceHz':hz,'outputStart':start,'sourceStart':src_start,'frames':length,
                                 'rawExpectedRelativePhaseRadians':expected,'sourceResidualDegrees':wrap(np.angle(sz[ch]/sz[0])-expected)*180/math.pi,
                                 'outputResidualDegrees':wrap(np.angle(z[ch]/z[0])-expected)*180/math.pi,
                                 'outputSourceRelativePhaseChangeDegrees':wrap(np.angle(z[ch]/z[0])-np.angle(sz[ch]/sz[0]))*180/math.pi}
                    row['phaseObservations'].append(observation);phases.append({'case':record['id'],'finer':j['finer'],'profile':j['profile'],**observation})
    rows.append(row)
    if j['block']==512 and record['id']<48:
        partner=next(p for p in processes if p['request']=={**j,'block':97});reference=pcm(root/f"case-{partner['id']:02d}"/'rendered.wav');delta=rendered.astype(np.float64)-reference.astype(np.float64)
        partitions.append({'case':record['id'],'partner':partner['id'],'exactSampleBytes':rendered.tobytes()==reference.tobytes(),'maxSampleDifference':float(np.abs(delta).max()),'rmsDifference':float(np.sqrt(np.mean(delta*delta)))})
for record in processes:
    if record['id'] not in [48,50]:continue
    reference=next(p for p in processes if p['request']=={**record['request'],'together':True});a=pcm(root/f"case-{record['id']:02d}"/'rendered.wav');b=pcm(root/f"case-{reference['id']:02d}"/'rendered.wav')
    policies.append({'apartCase':record['id'],'togetherCase':reference['id'],'exactSampleBytes':a.tobytes()==b.tobytes(),'maxSampleDifference':float(np.abs(a.astype(np.float64)-b.astype(np.float64)).max())})
summary={'format':'sc-warp-candidate-analysis-v1','observations':rows,'partitionComparisons':partitions,'groupPolicyComparisons':policies,
         'eventDetector':'post-run diagnostic: first 10% of annotated-window maximum, 32-frame causal RMS, source bias measured; two bias interpretations retained',
         'onsetEstimatorIsUniversalGroundTruth':False,'fullQualityQualified':False,'groupPhaseQualified':False,'QStretchPassed':False,'QPitchPassed':False,
         'phaseEstimator':'2048-frame Hann projection at exact known carrier; sign corrected, principal relative phase, source/leakage bias retained; stereo sustain only',
         'maximumRawBiasCorrectedEventErrorFrames':max(abs(o['rawBiasCorrectedReferenceErrorFrames']) for o in onsets),
         'maximumMapBiasCorrectedEventErrorFrames':max(abs(o['mapBiasCorrectedReferenceErrorFrames']) for o in onsets),
         'maximumMeasuredArrivalOffsetChangeFrames':max(abs(o['arrivalOffsetChangeFrames']) for o in onsets),
         'maximumSelectedStereoPhaseChangeDegrees':max(abs(o['outputSourceRelativePhaseChangeDegrees']) for o in phases),
         'resolvedOnsetObservations':sum(o['timingEstimateResolved'] for o in onsets),
         'censoredOnsetObservations':sum(not o['timingEstimateResolved'] for o in onsets),
         'maximumResolvedRawBiasErrorFrames':max(abs(o['rawBiasCorrectedReferenceErrorFrames']) for o in onsets if o['timingEstimateResolved']),
         'maximumResolvedMapBiasErrorFrames':max(abs(o['mapBiasCorrectedReferenceErrorFrames']) for o in onsets if o['timingEstimateResolved']),
         'exactPartitionComparisons':sum(p['exactSampleBytes'] for p in partitions),'partitionComparisonsCount':len(partitions),
         'analysisScriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'numpyVersion':np.__version__}
(root/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['observations','partitionComparisons','groupPolicyComparisons']}))
for finer in [False,True]:
    for family in ['impulse','attack']:
        for profile in ['constant','uniform-map','nonuniform-map']:
            subset=[o for o in onsets if o['finer']==finer and o['family']==family and o['profile']==profile and o['pitchMilliCents']==0]
            print(json.dumps({'engine':'R3' if finer else 'R2','family':family,'profile':profile,'observations':len(subset),
                              'maxRawBiasError':max(abs(o['rawBiasCorrectedReferenceErrorFrames']) for o in subset),
                              'maxMapBiasError':max(abs(o['mapBiasCorrectedReferenceErrorFrames']) for o in subset),
                              'maxArrivalOffsetChange':max(abs(o['arrivalOffsetChangeFrames']) for o in subset)}))
