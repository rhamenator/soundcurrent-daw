#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent retained geometry/PCM/process inspection, no processor or native replay."""
from pathlib import Path,PurePosixPath
from fractions import Fraction as F
import array,hashlib,json,math,struct,sys,zipfile
root=Path(__file__).resolve().parent;manifest=json.loads((root/'manifest.json').read_text());archive=root/'capture.zip';checks=0;sha=lambda d:hashlib.sha256(d).hexdigest()
def check(value,reason):
 global checks
 checks+=1
 if not value:raise AssertionError(reason)
def mapping(points,x):
 if x==points[-1][0]:return points[-1][1]
 for (a,b),(c,d) in zip(points,points[1:]):
  if a<=x<c:return b+(x-a)*(d-b)/(c-a)
 raise AssertionError('Independent map outside domain')
check(manifest['format']=='sc-anchored-stretch-capture-v1','Format')
check(archive.stat().st_size==manifest['bytes']<64*1024*1024 and sha(archive.read_bytes())==manifest['sha256'],'Archive hash/budget')
for key in ['nativeAudio','nativeWindowsReplayed','shippingAdopted','fullQualityQualified','productBinaryUploaded']:check(manifest[key] is False,'Scope')
with zipfile.ZipFile(archive) as z:
 check(len(z.namelist())==len(set(z.namelist()))==len(manifest['entries']) and set(z.namelist())==set(manifest['entries']),'Member set')
 check(sum(e.file_size for e in z.infolist())<128*1024*1024,'Payload budget')
 for info in z.infolist():
  path=PurePosixPath(info.filename);check(not path.is_absolute() and '..' not in path.parts and '\\' not in info.filename and info.file_size<5*1024*1024,'Member admission')
  data=z.read(info);check(len(data)==manifest['entries'][info.filename]['bytes'] and sha(data)==manifest['entries'][info.filename]['sha256'],'Member hash')
 get=lambda p:json.loads(z.read(p));summary=get('run/summary.json');records=get('run/processes.json');contract=get('run/contract.json');analysis=get('run/analysis.json');build=get('build-inputs.json')
 check(len(records)==summary['actualProcesses']==summary['terminalSuccesses']==24 and summary['seconds']<60,'Actual bank outcomes')
 check(summary['addressSpaceCeilingBytes']==512*1024*1024 and summary['childDeadlineSeconds']==10,'Recorded budget')
 check(summary['sourceBase']==build['productionBase']==contract['sourceBase']=='a08eb0e81114e21d24fff56905e42a4d5a9b2b29' and summary['sourceWasUncommitted'] and build['sourceWasUncommitted'],'Source scope')
 check(contract['frozenAtUtc']<summary['utc'],'Preregistration before rendering')
 for name,key in [('contract.json','contractSha256'),('render_probe.cpp','rendererSourceSha256'),('run_bank.py','runnerSha256'),('analyze_bank.py','analysisSourceSha256BeforeRun')]:check(sha(z.read('experiment/'+name))==summary[key],'Executed/preregistered source identity')
 check(analysis['analysisSourceSha256']==summary['analysisSourceSha256BeforeRun'] and analysis['contractSha256']==summary['contractSha256'] and z.read('experiment/contract.json')==z.read('run/contract.json'),'Analysis/source contract')
 check(summary['probeSha256']==build['executables']['renderer']['sha256'],'Recorded renderer binary identity')
 sourceReview=get('candidate-source/source-review.json')
 for file in sourceReview['files']:
  folder='signalsmith-stretch' if file['repository'].endswith('/signalsmith-stretch') else 'linear';data=z.read('candidate-source/'+folder+'/'+file['path']);check(sha(data)==file['sha256'] and len(data)==file['bytes'],'Reviewed primary source hash')
  check(hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==file['gitBlob'],'Primary git blob identity')
 for file in get('candidate-source/include-layout.json')['exactTrackedIncludeFiles']:
  data=z.read('candidate-source/linear/'+file['path']);check(sha(data)==file['sha256'] and data.decode()==file['text'],'Exact include wrapper')
 waves={};pcm={}
 for r in records:
  check(r['pid']>0 and r['exitCode']==0 and not r['timeout'] and r['seconds']<10 and r['stderr']=='','Terminal child outcome')
  report=get(f"run/case-{r['id']:02d}/render.json");check(report==r['result']==json.loads(r['stdout']) and report['request']==r['request'],'Report/process binding')
  check(report['frames']==32768 and report['target']==report['writtenFrames'] and report['generatedFrames']==report['target']+report['outputLatency'] and report['trimmedLeadingFrames']==report['outputLatency']==2880 and report['zeroFinalLookaheadFrames']==report['inputLatency']==2880,'Explicit latency/edge extent')
  check(report['seed']==20261010 and report['blockFrames']==5760 and report['intervalFrames']==1440,'Processor configuration')
  for key in ['nativeAudio','shippingAdopted','fullQualityQualified']:check(report[key] is False,'Renderer scope')
  points=[(F(v['output']),F(v['source'])) for v in report['points']];cursor=2880;at=0;ends=[]
  for step in report['schedule']:
   end=step['outputEnd'];exact=mapping(points,F(end));nextCursor=exact.numerator//exact.denominator+2880
   check(step['outputBegin']==at and 0<end-at<=r['request']['block'] and step['inputCursor']==cursor and step['nextInputCursor']==nextCursor and 0<=nextCursor-cursor<=512,'Bounded continuous exact scheduler')
   check(step['inverseEnd']==[exact.numerator//exact.denominator,exact.numerator%exact.denominator,exact.denominator],'Exact inverse and retained residual')
   at=end;cursor=nextCursor;ends.append(end)
  check(at==report['target'] and cursor==32768+2880 and all(p['output'] in ends for p in report['points'][1:]),'Complete anchor/end schedule')
  for name,count in [('source',32768),('generated',report['target']+2880),('rendered',report['target'])]:
   digest=r['waves'][name]['sha256'];data=z.read('waves/'+digest+'.wav');check(sha(data)==digest and len(data)==r['waves'][name]['bytes'],'Complete wave identity')
   check(data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt ' and data[36:40]==b'data' and struct.unpack_from('<I',data,4)[0]==len(data)-8,'Owned WAV')
   fmt=struct.unpack_from('<HHIIHH',data,20);check(fmt==(3,report['channels'],48000,48000*report['channels']*4,report['channels']*4,32),'Float channel/rate layout')
   check(len(data)==44+count*report['channels']*4 and struct.unpack_from('<I',data,40)[0]==len(data)-44,'Complete frame count')
   values=array.array('f');values.frombytes(data[44:]);
   if sys.byteorder!='little':values.byteswap()
   check(values.itemsize==4 and all(math.isfinite(x) for x in values),'Finite full PCM');waves[(r['id'],name)]=data[44:];pcm[(r['id'],name)]=values
  check(waves[(r['id'],'rendered')]==waves[(r['id'],'generated')][2880*report['channels']*4:],'Prescribed latency trim is exact slice')
 onsets=[o for c in analysis['cases'] for o in c['onsets']];phases=[o for c in analysis['cases'] for o in c['phase']];cancellations=[o for c in analysis['cases'] for o in c['cancellation']]
 check(len(onsets)==analysis['onsetObservations']==432 and sum(o['resolved'] for o in onsets)==analysis['resolvedOnsets']==432 and analysis['unresolvedOnsets']==0,'Window-resolved diagnostics scope')
 check(sum(o['timingDiagnosticPassed'] for o in onsets)==analysis['timingDiagnosticPassed']==176,'Diagnostic timing outcomes')
 for o in onsets:
  r=records[o['case']];pts=[(F(p['source']),F(p['output'])) for p in r['result']['points']];expected=mapping(pts,F(o['event']+o['sourceBiasFrames']))+o['expectedPhysicalDelayFrames'];error=F(o['outputDetector']['frame'])-expected
  check(o['expectedDetectorFrame']==[expected.numerator,expected.denominator] and o['errorFrames']==float(error) and o['timingDiagnosticPassed']==(abs(error)<48),'Independent exact bias/timing interpretation')
 check(max(abs(o['errorFrames']) for o in onsets)==analysis['maximumResolvedTimingErrorFrames']==146.5 and max(abs(o['arrivalOffsetChangeFrames']) for o in onsets)==analysis['maximumArrivalOffsetChangeFrames']==1,'Recorded timing/group maxima')
 check(len(phases)==analysis['phaseObservations']==420 and sum(o['diagnosticPassed'] for o in phases)==analysis['phaseDiagnosticPassed']==155,'Phase diagnostic scope')
 check(len(cancellations)==analysis['cancellationObservations']==24 and sum(o['diagnosticPassed'] for o in cancellations)==analysis['cancellationDiagnosticPassed']==20,'Cancellation outcomes')
 for o in cancellations:
  r=records[o['case']];ch=r['result']['channels'];a=pcm[(o['case'],'source')];b=pcm[(o['case'],'rendered')];left,right=o['pair'];check(all(float(a[f+left])+float(a[f+right])==0 for f in range(0,len(a),ch)),'Original exact cancellation');value=max(abs(float(b[f+left])+float(b[f+right])) for f in range(0,len(b),ch));check(value==o['maximumPairSum'] and o['diagnosticPassed']==(value<=1e-6),'Independent output pair cancellation')
 for o in analysis['partitionComparisons']:
  a,b=pcm[(o['case'],'rendered')],pcm[(o['partner'],'rendered')];check((waves[(o['case'],'rendered')]==waves[(o['partner'],'rendered')])==o['exactSampleBytes'],'Partition sample identity');check(max(abs(float(x)-float(y)) for x,y in zip(a,b))==o['maximumSampleDifference'],'Independent partition difference')
 for key in ['fullQualityQualified','QStretchPassed','groupPhaseQualified','nativeAudio']:check(analysis[key] is False,'No unsupported quality promotion')
print(json.dumps({'retainedInspectionChecks':checks,'actualRecordedProcesses':24,'completeWaveReferences':72,'nativeAudio':False,'processorReplayed':False,'fullQualityQualified':False}))
