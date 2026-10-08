#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Read-only independent WAV/EQ oracle; does not replay native audio.
from pathlib import Path
import sys,json,hashlib,math,struct
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_input_acquisition import float_wav
if len(sys.argv)!=2: raise SystemExit('Supply a retained native48 kHz project directory')
p=Path(sys.argv[1]).resolve()
s=json.loads((p/'project.json').read_text());report=json.loads((p/'probe.json').read_text())
a=s['assets'][0];rawfile=p/a['path'];exportfile=p/'exports/native.wav'
assert hashlib.sha256(rawfile.read_bytes()).hexdigest()==a['sha256']==report['assetSha256']
assert hashlib.sha256(exportfile.read_bytes()).hexdigest()==report['exportSha256']
raw=float_wav(rawfile);out=float_wav(exportfile)
assert len(raw)==len(out)==96000 and all(math.isfinite(v) for v in raw)
assert any(v!=0 for v in raw)
values=list(map(float,raw))
for band in s['tracks'][0]['processors'][0]['bands']:
 if band['gainDb']==0: continue
 A=10**(band['gainDb']/40);w=2*math.pi*band['frequencyHz']/s['sampleRate'];alpha=math.sin(w)/(2*band['q'])
 a0=1+alpha/A;b0=(1+alpha*A)/a0;b1=-2*math.cos(w)/a0;b2=(1-alpha*A)/a0;a1=b1;a2=(1-alpha/A)/a0
 x1=x2=y1=y2=0.;pred=[]
 for x in values:
  y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2
  pred.append(y);x2=x1;x1=x;y2=y1;y1=y
 values=pred
rounded=[struct.unpack('<f',struct.pack('<f',v))[0] for v in values]
error=max(abs(a-b) for a,b in zip(rounded,out));assert error<=1e-7
assert raw!=out and report['reopenedAndExported'] and report['defaultsUnchanged']
assert not report['firstBridgeFault'] and report['firstInputError'] is None and report['streamFailure'] is None
assert report['cppAllocations']==report['cppFrees']==0
result={'frames':len(raw),'sampleRate':s['sampleRate'],'rawSha256':a['sha256'],'exportSha256':report['exportSha256'],'independentDirectFormI':True,'maximumFloat32SampleError':error,'nonzeroRawSamples':sum(v!=0 for v in raw),'rawPeak':max(abs(v) for v in raw),'sourceCoverageOrPhysicalLatencyQualified':False,'nativeGUIOrInstallerQualified':False}
assert report['workflowAccepted']
assert report['nonzeroSdkChannel1Samples']==result['nonzeroRawSamples']
print(json.dumps(result,indent=2))
