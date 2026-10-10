#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Analysis of retained actual outputs; no additional DSP/installer/audio replay."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import argparse,hashlib,json,math,struct
import numpy as np
p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);args=p.parse_args();folder=args.folder.resolve();rows=json.loads((folder/'results.json').read_text());processes=json.loads((folder/'processes.json').read_text())
def pcm(path):
 data=path.read_bytes();assert len(data)<=16*1024*1024 and data[:4] in [b'RIFF',b'RF64'] and data[8:12]==b'WAVE';at=12;size64=None;fmt=None;payload=None
 while at+8<=len(data):
  tag=data[at:at+4];size=struct.unpack_from('<I',data,at+4)[0];at+=8
  if tag==b'data' and size==0xffffffff:size=size64
  assert size is not None and size<=len(data)-at
  if tag==b'ds64':size64=struct.unpack_from('<Q',data,at+8)[0]
  if tag==b'fmt ':fmt=struct.unpack_from('<HHIIHH',data,at)
  if tag==b'data':payload=data[at:at+size]
  at+=size+(size&1)
 assert at==len(data) and fmt is not None and payload is not None
 return np.frombuffer(payload,dtype='<f4').reshape(-1,fmt[1])

tones=[];seen=set()
for r in rows:
 if r['family']!='sustain':continue
 operation=r['wholeRequest']['operation']
 if operation in seen:continue
 seen.add(operation);record=next(v for v in processes if v['kind']=='worker' and v['request']['operation']==operation)
 path=Path(record['root'])/'exports/visible.wav';audio=pcm(path);length=2**int(math.floor(math.log2(len(audio)//2)));start=(len(audio)-length)//2
 for ch in range(audio.shape[1]):
  segment=audio[start:start+length,ch].astype(np.float64);spectrum=np.abs(np.fft.rfft(segment*np.hanning(length)));frequencies=np.fft.rfftfreq(length,1/48000)
  for raw_hz in [440.,730.]:
   expected=raw_hz*2**(r['spec']['pitchMilliCents']/1200000);band=np.flatnonzero((frequencies>=expected*.88)&(frequencies<=expected*1.12));peak=int(band[np.argmax(spectrum[band])]);log=np.log(np.maximum(spectrum[peak-1:peak+2],np.finfo(float).tiny));den=log[0]-2*log[1]+log[2];offset=.5*(log[0]-log[2])/den if den else 0.;measured=(peak+offset)*48000/length
   tones.append({'operation':operation,'channel':ch,'sourceHz':raw_hz,'expectedHz':expected,'measuredHz':float(measured),'errorCents':float(1200*math.log2(measured/expected)),'analysisFrames':length,'analysis':'central Hann FFT, three-bin log-magnitude interpolation, no analysis zero padding','waveSha256':hashlib.sha256(path.read_bytes()).hexdigest()})
summary={'format':'sc-region-quality-analysis-v1','cases':len(rows),'exactUnityComparisons':sum(r['metrics']['exactFloatSampleBytes'] for r in rows),'nonUnityComparisons':sum(not(r['spec']['timeNumerator']==r['spec']['timeDenominator'] and r['spec']['pitchMilliCents']==0) for r in rows),
         'nonUnityDifferentComparisons':sum(not r['metrics']['exactFloatSampleBytes'] for r in rows),
         'maximumSampleDifference':max(r['metrics']['maxSampleDifference'] for r in rows),
         'maximumRelativeRmsDifference':max(r['metrics']['relativeRmsDifference'] or 0 for r in rows),
         'maximumSteadyToneFrequencyErrorCents':max(abs(r['errorCents']) for r in tones),'steadyToneFrequencyObservations':tones,
         'rawChannelDelayFrames':[3*ch for ch in range(8)],
         'channelDelayInterpretation':'Raw delayed-copy offsets, nominal map-scaled displacements and observed waveform correlation lags are distinct. Their differences alone do not define phase-coherent grouped warp quality; pitch-preserving carrier phase and envelope time have separate contracts.',
         'wholeSourceIsAcousticGroundTruth':False,'peakIsUniversalEventTimingOracle':False,'QStretchPassed':False,'QPitchPassed':False,
         'numpyVersion':np.__version__,'analysisScriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
(folder/'analysis.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({k:v for k,v in summary.items() if k!='steadyToneFrequencyObservations'}))
