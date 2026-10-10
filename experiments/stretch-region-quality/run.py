#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded actual-helper context/edge/group diagnostics, never acoustic parity.
Uses original generated signals, independently decoded WAVs and a C++ parent
verifier/shared reader. Retains every outcome; it never opens an audio endpoint.
"""
import os
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import argparse,hashlib,json,math,queue,struct,subprocess,threading,time,uuid,sys
from fractions import Fraction
import numpy as np

parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('worker',type=Path);parser.add_argument('probe',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args()
worker=args.worker.resolve();probe=args.probe.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
if sys.platform=='linux':
 import resource
 resource.setrlimit(resource.RLIMIT_AS,(512*1024*1024,512*1024*1024))
started=time.monotonic();records=[];whole={};source_hashes={};checks=0
sha=lambda data:hashlib.sha256(data).hexdigest()
def check(condition,message):
 global checks
 checks+=1
 if not condition:raise AssertionError(message)
def save(name,value):
 (output/name).write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n')
def spec(first=16320,fraction=0,n=3,d=2,pitch=0,before=4095,after=4096,frames=128):
 return {'first':first,'firstFraction':fraction,'firstDenominator':2 if fraction else 1,'frames':frames,
         'timeNumerator':n,'timeDenominator':d,'pitchMilliCents':pitch,'formantPreserved':True,
         'contextEnabled':True,'contextBefore':before,'contextAfter':after}
def fixture(family,channels):
 frames=32768;at=np.arange(frames,dtype=np.float64);audio=np.zeros((frames,channels),dtype=np.float32)
 for ch in range(channels):
  delay=3*ch;gain=[1.,-.5,.25,-.125,.75,-.375,.1875,-.09375][ch]
  for center in [64,16384,32704]:
   if family=='impulse':audio[center+delay,ch]=np.float32(1.5*gain)
   elif family=='attack':
    relative=at-center-delay;valid=(relative>=0)&(relative<2048);t=relative[valid]
    envelope=np.minimum(1.,t/8)*np.exp(-t/384)
    audio[valid,ch]+=np.asarray(gain*envelope*(.8*np.sin(2*np.pi*440*t/48000)+.4*np.sin(2*np.pi*880*t/48000)+.2*np.sin(2*np.pi*1320*t/48000)),dtype=np.float32)
  if family=='sustain':audio[:,ch]=np.asarray(gain*(.7*np.sin(2*np.pi*440*(at-delay)/48000)+.2*np.sin(2*np.pi*730*(at-delay)/48000)),dtype=np.float32)
 payload=audio.astype('<f4',copy=False).tobytes();fmt=struct.pack('<HHIIHH',3,channels,48000,48000*channels*4,channels*4,32)
 return b'RIFF'+struct.pack('<I',36+len(payload))+b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
fixtures={(family,ch):fixture(family,ch) for family,ch in [('impulse',2),('attack',2),('sustain',2),('impulse',8),('attack',8)]}
source_dir=output/'sources';source_dir.mkdir()
for (family,ch),raw in fixtures.items():
 name=f'{family}-{ch}.wav';(source_dir/name).write_bytes(raw);source_hashes[name]=sha(raw)

def run_probe(mode,root,value,ready=None):
 command=[str(probe),mode,str(root),json.dumps(value,separators=(',',':'))]
 if ready is not None:command.append(json.dumps(ready,separators=(',',':')))
 p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:out,err=p.communicate(timeout=20)
 except subprocess.TimeoutExpired:
  p.kill();out,err=p.communicate();records.append({'kind':'probe','mode':mode,'root':str(root),'pid':p.pid,'exitCode':p.returncode,'deadlineExceeded':True,'stdout':out,'stderr':err});raise
 records.append({'kind':'probe','mode':mode,'root':str(root),'pid':p.pid,'exitCode':p.returncode,'stdout':out,'stderr':err})
 check(p.returncode==0 and len(out)<=32768 and len(err)<=32768,'Parent diagnostic failed: '+err)
 return json.loads(out)

def render(label,family,channels,value):
 check(time.monotonic()-started<240,'Global experiment deadline')
 root=output/label;root.mkdir();(root/'media').mkdir();jobs=root/'media/derived';jobs.mkdir();raw=fixtures[family,channels];(root/'media/signal-été.wav').write_bytes(raw)
 request=run_probe('prepare',root,value);command=[str(worker),'--project-root',str(root),'--jobs-root',str(jobs),'--memory-mib','256','--maximum-input-frames','131072','--maximum-output-bytes',str(16*1024*1024),'--deadline-ms','10000']
 p=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE);p.stdin.write(json.dumps(request).encode());p.stdin.close()
 reports=queue.Queue(maxsize=4);buffers={'stdout':bytearray(),'stderr':bytearray()};overflow=threading.Event()
 def pump(stream,name):
  try:
   if name=='stdout':
    while True:
     line=stream.readline(16385)
     if not line:break
     if len(line)>16384 or len(buffers[name])+len(line)>65536:overflow.set();p.kill();break
     buffers[name].extend(line)
     try:reports.put_nowait(json.loads(line))
     except (queue.Full,json.JSONDecodeError):overflow.set();p.kill();break
   else:
    while True:
     chunk=stream.read(1024)
     if not chunk:break
     if len(buffers[name])+len(chunk)>32768:overflow.set();p.kill();break
     buffers[name].extend(chunk)
  finally:stream.close()
 threads=[threading.Thread(target=pump,args=(p.stdout,'stdout')),threading.Thread(target=pump,args=(p.stderr,'stderr'))]
 for t in threads:t.start()
 ready=None;failure=None;begin=time.monotonic()
 try:
  ready=reports.get(timeout=12);check(ready.get('event')=='ready','Helper refused before ready')
  check(run_probe('ready',root,request,ready)['readyVerified'] is True,'Parent did not verify ready identity')
  (jobs/request['operation']/'start.request').write_text('start\n')
  p.wait(timeout=12)
 except Exception as error:
  failure=repr(error)
  if p.poll() is None:p.kill()
  p.wait(timeout=5)
 finally:
  for t in threads:t.join(timeout=5)
  reports_raw=buffers['stdout'].decode('utf-8',errors='replace');errors=buffers['stderr'].decode('utf-8',errors='replace')
  record={'kind':'worker','label':label,'root':str(root),'pid':p.pid,'exitCode':p.returncode,'seconds':time.monotonic()-begin,'request':request,'stdout':reports_raw,'stderr':errors,'failure':failure,'overflow':overflow.is_set()};records.append(record)
  save('processes.json',records)
 check(failure is None and p.returncode==0 and not overflow.is_set(),'Actual helper failed: '+str(record))
 receipt=json.loads(reports_raw.splitlines()[-1]);check(receipt['complete'] is True,'No complete marker');marker=jobs/request['operation']/'complete.json';check(json.loads(marker.read_text())==receipt,'Marker/stdout differ')
 result=run_probe('adopt',root,request,ready);check(result['rawSha256']==sha(raw) and sha((root/'media/signal-été.wav').read_bytes())==sha(raw),'Original bytes changed')
 return root,request,receipt,result

def wave(path):
 data=path.read_bytes();check(len(data)<=16*1024*1024 and data[:4] in [b'RIFF',b'RF64'] and data[8:12]==b'WAVE','Invalid bounded WAV');at=12;sizes=None;fmt=None;pcm=None
 while at+8<=len(data):
  tag=data[at:at+4];size=struct.unpack_from('<I',data,at+4)[0];at+=8
  if tag==b'data' and size==0xffffffff:check(sizes is not None,'Missing ds64');size=sizes[1]
  check(size<=len(data)-at,'Truncated chunk')
  if tag==b'ds64':sizes=struct.unpack_from('<QQQ',data,at)
  if tag==b'fmt ':
   fmt=struct.unpack_from('<HHIIHH',data,at);encoding=fmt[0]
   if encoding==65534:check(size>=40 and data[at+24:at+40]==bytes.fromhex('0300000000001000800000aa00389b71'),'Non-float extensible WAV');encoding=3
   check(encoding==3 and fmt[2]==48000 and fmt[5]==32,'Unexpected float source format')
  if tag==b'data':check(pcm is None,'Repeated data');pcm=data[at:at+size]
  at+=size+(size&1)
 check(at==len(data) and fmt is not None and pcm is not None and len(pcm)%(4*fmt[1])==0,'Invalid waveform geometry')
 audio=np.frombuffer(pcm,dtype='<f4').reshape(-1,fmt[1]);check(np.isfinite(audio).all(),'Nonfinite samples');return audio,pcm,sha(data)

def correlation(context,reference,max_lag):
 best=None;zero=None
 for lag in range(-max_lag,max_lag+1):
  if lag>0:a,b=context[lag:],reference[:-lag]
  elif lag<0:a,b=context[:lag],reference[-lag:]
  else:a,b=context,reference
  if len(a)<16:continue
  a=a.astype(np.float64);b=b.astype(np.float64);a=a-a.mean();b=b-b.mean();den=math.sqrt(float(np.dot(a,a)*np.dot(b,b)))
  if den==0:continue
  score=float(np.dot(a,b))/den
  if lag==0:zero=score
  # Prefer the smallest absolute lag for numerical ties, avoiding a flat/tone
  # match falsely becoming the largest admitted lag.
  if best is None or score>best[1]+1e-12 or (abs(score-best[1])<=1e-12 and abs(lag)<abs(best[0])):best=(lag,score)
 return {'zeroLag':zero,'bestLag':best[0] if best else None,'bestScore':best[1] if best else None,'maximumLag':max_lag,'minimumOverlap':16}

def metrics(context,reference,family,value,channels):
 a=context.astype(np.float64);b=reference.astype(np.float64);diff=a-b;rms=lambda x:math.sqrt(float(np.mean(x*x)))
 result={'maxSampleDifference':float(np.max(np.abs(diff))),'differenceRms':rms(diff),'contextRms':rms(a),'referenceRms':rms(b),'exactFloatSampleBytes':context.tobytes()==reference.tobytes(),'channelMetrics':[]}
 result['relativeRmsDifference']=result['differenceRms']/result['referenceRms'] if result['referenceRms'] else None
 for ch in range(channels):
  x,y=a[:,ch],b[:,ch];energy=x*x;total=float(energy.sum());peak=int(np.argmax(np.abs(x)));ref_peak=int(np.argmax(np.abs(y)));max_lag=min(128,max(1,len(x)//4))
  center={0:64,16320:16384,32640:32704}[value['first']]+3*ch
  nominal=Fraction(center-value['first'])-Fraction(value['firstFraction'],value['firstDenominator']);nominal*=Fraction(value['timeNumerator'],value['timeDenominator'])
  result['channelMetrics'].append({'channel':ch,'peakIndex':peak,'referencePeakIndex':ref_peak,'peakDisplacementFromReference':peak-ref_peak,
     'nominalImpulseOrAttackFrame':[nominal.numerator,nominal.denominator],
     'peakMinusNominalDiagnostic':peak-float(nominal) if family!='sustain' and total else None,
     'energyCentroid':float(np.dot(np.arange(len(x)),energy))/total if total else None,
     'energyQuantiles':[int(np.searchsorted(np.cumsum(energy),total*q)) for q in [.05,.5,.95]] if total else None,
     'peakLinear':float(np.max(np.abs(x))),'samplesAboveUnity':int(np.count_nonzero(np.abs(x)>1)),
     'contextReferenceCorrelation':correlation(x,y,max_lag),
     'channel0Correlation':correlation(x*np.sign([1.,-.5,.25,-.125,.75,-.375,.1875,-.09375][ch]),a[:,0],max_lag),
     'nominalChannelDelay':float(Fraction(3*ch*value['timeNumerator'],value['timeDenominator']))})
 return result

points=[(1,1,0),(3,2,0),(3,2,700007)];cases=[]
for family in ['impulse','attack','sustain']:
 for fraction in [0,1]:
  for n,d,pitch in points:cases.append((family,2,spec(fraction=fraction,n=n,d=d,pitch=pitch),'interior-odd'))
for n,d,pitch in [(1,4,-2400000),(4,1,2400000)]:cases.append(('impulse',2,spec(n=n,d=d,pitch=pitch),'extreme'))
for n,d,pitch in points:cases.append(('impulse',2,spec(n=n,d=d,pitch=pitch,before=4096,after=4096),'interior-even'))
for first in [0,32640]:
 for fraction in [0,1]:
  for n,d,pitch in points[:2]:cases.append(('impulse',2,spec(first=first,fraction=fraction,n=n,d=d,pitch=pitch,before=0 if first==0 else 8191,after=8191 if first==0 else 0),'asset-edge'))
for n,d,pitch in points+[(1,4,-2400000),(4,1,2400000)]:cases.append(('impulse',8,spec(n=n,d=d,pitch=pitch),'grouped-discrete'))
cases.append(('attack',8,spec(),'grouped-attack'))
check(len(cases)==37,'Diagnostic case count changed');results=[]
try:
 for index,(family,channels,value,kind) in enumerate(cases):
  key=(family,channels,value['firstFraction'],value['timeNumerator'],value['timeDenominator'],value['pitchMilliCents'])
  if key not in whole:
   wspec=spec(first=0,fraction=value['firstFraction'],n=value['timeNumerator'],d=value['timeDenominator'],pitch=value['pitchMilliCents'],before=0,after=0,frames=32768)
   whole[key]=render('whole-'+str(len(whole)),family,channels,wspec)
  wroot,wrequest,wreceipt,wresult=whole[key]
  label=f'case-{index:02d}-{family}-{channels}';root,request,receipt,result=render(label,family,channels,value)
  check(result['frames']==(value['frames']*value['timeNumerator']+value['timeDenominator']-1)//value['timeDenominator'],'Visible duration differs from nominal selection')
  reference=run_probe('crop',wroot,{'rawOffset':value['first'],'visibleFrames':result['frames'],'relative':f'exports/reference-{index:02d}.wav'})
  actual,pcm,actual_hash=wave(root/result['relative']);ref,ref_pcm,ref_hash=wave(wroot/reference['relative']);check(actual.shape==ref.shape==(result['frames'],channels),'Comparison crop shapes differ')
  if value['timeNumerator']==value['timeDenominator'] and value['pitchMilliCents']==0:
   check(pcm==ref_pcm,'Unity context differs from corresponding full-source crop')
   if not value['firstFraction']:
    raw=fixtures[family,channels][44+value['first']*channels*4:44+(value['first']+value['frames'])*channels*4]
    check(pcm==raw,'Integer unity changed original sample bits or channel order')
  row={'id':index,'kind':kind,'family':family,'channels':channels,'spec':value,'request':request,'receipt':receipt,'wholeRequest':wrequest,'wholeReceipt':wreceipt,
       'adoption':result,'reference':reference,'contextWaveSha256':actual_hash,'referenceWaveSha256':ref_hash,'metrics':metrics(actual,ref,family,value,channels)}
  results.append(row);save('results.json',results);print(json.dumps({'case':index,'family':family,'channels':channels,'kind':kind,'maxSampleDifference':row['metrics']['maxSampleDifference'],'relativeRmsDifference':row['metrics']['relativeRmsDifference']}),flush=True)
  check(time.monotonic()-started<240,'Global experiment deadline exceeded')
 check(len(whole)==26 and sum(r['kind']=='worker' for r in records)==63,'Actual render matrix incomplete')
 summary={'format':'sc-region-quality-observations-v1','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
          'cases':len(results),'wholeJobs':len(whole),'actualWorkerProcesses':sum(r['kind']=='worker' for r in records),'actualProbeProcesses':sum(r['kind']=='probe' for r in records),
          'checks':checks,'seconds':time.monotonic()-started,'workerSha256':sha(worker.read_bytes()),'probeSha256':sha(probe.read_bytes()),'runScriptSha256':sha(Path(__file__).read_bytes()),
          'numpyVersion':np.__version__,'sourceHashes':source_hashes,'nativePlatform':sys.platform,'addressSpaceCeilingBytes':512*1024*1024,'perWorkerMemoryBytes':256*1024*1024,
          'globalDeadlineSeconds':240,'workerDeadlineMilliseconds':10000,'parentTimeoutSeconds':20,
          'nativeAudio':False,'nativeWindowsReplayed':False,'productionChanged':False,'automaticContextQualified':False,'fullProcessingQuality':False,'QStretchPassed':False,
          'unityContextCopyExact':True,'allSharedLiveExportExact':True,'callbackAudit':0}
 save('summary.json',summary);save('processes.json',records);print(json.dumps(summary),flush=True)
except BaseException as error:
 save('failure.json',{'error':repr(error),'casesCompleted':len(results),'seconds':time.monotonic()-started,'nativeAudio':False});save('processes.json',records);raise
