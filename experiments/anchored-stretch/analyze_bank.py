#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Preregistered synthetic diagnostics; not perceptual or full group qualification."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
from fractions import Fraction as F
import argparse,hashlib,json,math,struct
import numpy as np
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);args=p.parse_args();root=args.folder.resolve();records=json.loads((root/'processes.json').read_text());contract=json.loads((root/'contract.json').read_text());events=contract['events'];rows=[];onsets=[];phase=[];cancel=[];pre=[];partitions=[]
def pcm(path):
 data=path.read_bytes();assert len(data)<4*1024*1024 and data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt ' and data[36:40]==b'data'
 fmt=struct.unpack_from('<HHIIHH',data,20);assert fmt[0]==3 and fmt[2]==48000 and fmt[5]==32
 assert len(data)==44+struct.unpack_from('<I',data,40)[0];a=np.frombuffer(data[44:],dtype='<f4').reshape(-1,fmt[1]);assert np.isfinite(a).all();return a

def mapped(points,x,inverse=False):
 pts=[(F(v['source']),F(v['output'])) for v in points]
 if inverse:pts=[(b,a) for a,b in pts]
 if x==pts[-1][0]:return pts[-1][1]
 for (a,b),(c,d) in zip(pts,pts[1:]):
  if a<=x<c:return b+(x-a)*(d-b)/(c-a)
 raise AssertionError('Outside exact map')
def detector(audio,ch,event):
 x=audio[:,ch].astype(np.float64);rms=np.sqrt(np.maximum(0,np.convolve(x*x,np.ones(32)/32,mode='full')[:len(x)]));start=max(0,int(event)-3072);end=min(len(x),int(event)+4096);window=rms[start:end];maximum=float(window.max());found=np.flatnonzero(window>=maximum*.1) if maximum>0 else np.array([])
 if not len(found):return {'resolved':False,'reason':'no landmark','window':[start,end]}
 frame=int(found[0])+start;return {'frame':frame,'maximumWindowRms':maximum,'thresholdRms':maximum*.1,'window':[start,end],'resolved':frame!=start and frame!=end-1,'reason':'window boundary' if frame==start or frame==end-1 else 'resolved'}
for r in records:
 assert r['exitCode']==0 and r['stderr']==''
 j=r['request'];report=r['result'];folder=root/f"case-{r['id']:02d}";a=pcm(folder/'source.wav');b=pcm(folder/'rendered.wav');g=pcm(folder/'generated.wav');assert g.shape[0]==report['target']+report['outputLatency'] and np.array_equal(b,g[report['outputLatency']:])
 row={'id':r['id'],'request':j,'onsets':[],'phase':[],'cancellation':[]};points=report['points'];channels=report['channels']
 if j['family']!='sustain':
  for event in events:
   target=mapped(points,F(event));group=[]
   for ch in range(channels):
    delay=0 if j['family']=='cancellation' else 3*ch;s=detector(a,ch,event+delay);o=detector(b,ch,float(target)+delay);resolved=s['resolved'] and o['resolved'];entry={'case':r['id'],'event':event,'channel':ch,'sourceDetector':s,'outputDetector':o,'resolved':resolved,'expectedPhysicalDelayFrames':delay}
    if resolved:
     bias=s['frame']-event-delay;expected=target+delay+mapped(points,F(event+bias))-target;error=F(o['frame'])-expected;entry.update(sourceBiasFrames=bias,expectedDetectorFrame=[expected.numerator,expected.denominator],errorFrames=float(error),timingDiagnosticPassed=abs(error)<48)
    else:entry.update(timingDiagnosticPassed=False)
    group.append(entry);onsets.append(entry)
    t=int(target)+delay;before=b[max(0,t-1024):max(0,t-48),ch].astype(np.float64);after=b[t:min(len(b),t+1536),ch].astype(np.float64);be=float(np.mean(before*before));ae=float(np.mean(after*after));db=10*math.log10(be/ae) if be>0 and ae>0 else (-300. if ae>0 else None);pv={'case':r['id'],'event':event,'channel':ch,'preRms':math.sqrt(be),'eventRms':math.sqrt(ae),'preEventRatioDb':db,'resolved':ae>0,'diagnosticPassed':db is not None and db<=-40};pre.append(pv)
   for e in group:
    if e['resolved'] and group[0]['resolved']:
     delta=(e['outputDetector']['frame']-group[0]['outputDetector']['frame'])-(e['sourceDetector']['frame']-group[0]['sourceDetector']['frame']);e.update(arrivalOffsetChangeFrames=delta,arrivalDiagnosticPassed=delta==0)
    else:e.update(arrivalDiagnosticPassed=False)
   row['onsets'].extend(group)
 if j['family']=='sustain':
  for segment,(left,right) in enumerate(zip(points,points[1:])):
   center=(left['output']+right['output'])//2;sourceCenter=mapped(points,F(center),True);start=center-1024;srcStart=int(sourceCenter)-1024
   if start<0 or start+2048>len(b) or srcStart<0 or srcStart+2048>len(a):continue
   taper=np.hanning(2048);n=np.arange(2048);sign=np.sign(a[1,:])
   gains=np.array([1,-.5,.25,-.125,.75,-.375,.1875,-.09375])[:channels]
   for hz in [440.,730.]:
    carrier=np.exp(-2j*np.pi*hz*n/48000);z=np.sum(b[start:start+2048,:].astype(np.float64)*taper[:,None]*carrier[:,None],axis=0)*np.sign(gains);sz=np.sum(a[srcStart:srcStart+2048,:].astype(np.float64)*taper[:,None]*carrier[:,None],axis=0)*np.sign(gains)
    for ch in range(1,channels):
     resolved=bool(min(abs(z[ch]),abs(z[0]),abs(sz[ch]),abs(sz[0]))>1e-9);change=float(np.angle(np.exp(1j*(np.angle(z[ch]/z[0])-np.angle(sz[ch]/sz[0])))))*180/math.pi if resolved else None
     entry={'case':r['id'],'segment':segment,'channel':ch,'hz':hz,'outputStart':start,'sourceStart':srcStart,'frames':2048,'resolved':resolved,'phaseChangeDegrees':change,'diagnosticPassed':resolved and abs(change)<=.0001};phase.append(entry);row['phase'].append(entry)
 if j['family']=='cancellation':
  for ch in range(0,channels,2):
   assert np.max(np.abs(a[:,ch].astype(np.float64)+a[:,ch+1]))==0
   maximum=float(np.max(np.abs(b[:,ch].astype(np.float64)+b[:,ch+1])));entry={'case':r['id'],'pair':[ch,ch+1],'maximumPairSum':maximum,'diagnosticPassed':maximum<=1e-6};cancel.append(entry);row['cancellation'].append(entry)
 if j['block']==512:
  partner=next(t for t in records if t['request']=={**j,'block':97});other=pcm(root/f"case-{partner['id']:02d}"/'rendered.wav');partitions.append({'case':r['id'],'partner':partner['id'],'exactSampleBytes':b.tobytes()==other.tobytes(),'maximumSampleDifference':float(np.max(np.abs(b.astype(np.float64)-other.astype(np.float64))))})
 rows.append(row)
summary={'format':'sc-anchored-stretch-analysis-v1','analysisSourceSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'contractSha256':hashlib.sha256((root/'contract.json').read_bytes()).hexdigest(),'numpyVersion':np.__version__,'cases':rows,'preecho':pre,'partitionComparisons':partitions,'onsetObservations':len(onsets),'resolvedOnsets':sum(o['resolved'] for o in onsets),'unresolvedOnsets':sum(not o['resolved'] for o in onsets),'timingDiagnosticPassed':sum(o['timingDiagnosticPassed'] for o in onsets),'maximumResolvedTimingErrorFrames':max(abs(o['errorFrames']) for o in onsets if o['resolved']),'maximumArrivalOffsetChangeFrames':max(abs(o['arrivalOffsetChangeFrames']) for o in onsets if 'arrivalOffsetChangeFrames' in o),'phaseObservations':len(phase),'phaseDiagnosticPassed':sum(o['diagnosticPassed'] for o in phase),'maximumResolvedPhaseChangeDegrees':max(abs(o['phaseChangeDegrees']) for o in phase if o['resolved']),'cancellationObservations':len(cancel),'cancellationDiagnosticPassed':sum(o['diagnosticPassed'] for o in cancel),'maximumCancellationPairSum':max(o['maximumPairSum'] for o in cancel),'preechoObservations':len(pre),'preechoDiagnosticPassed':sum(o['diagnosticPassed'] for o in pre),'exactPartitionPairs':sum(o['exactSampleBytes'] for o in partitions),'maximumPartitionSampleDifference':max(o['maximumSampleDifference'] for o in partitions),'fullQualityQualified':False,'QStretchPassed':False,'groupPhaseQualified':False,'nativeAudio':False}
(root/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k not in ['cases','preecho','partitionComparisons']}))
