#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned original-context worker, using actual published mixed-pair ancestry."""
from pathlib import Path
import argparse,copy,hashlib,json,subprocess,sys,tempfile,uuid
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser();names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','context_fixture','repeat_fixture']
for name in names:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();sha=lambda b:hashlib.sha256(b).hexdigest();binary=sha(a.worker.read_bytes());rows=[]
command=[sys.executable,str(Path(__file__).with_name('comp_protected_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:6]]+['--case','48000-uniform','--evidence',str(scope/'input')]
with (scope/'input.stdout').open('w') as out,(scope/'input.stderr').open('w') as err:
 result=subprocess.run(command,stdout=out,stderr=err);assert result.returncode==0,scope
root=next((scope/'input').glob('run-*/case-48000-uniform/project-*'))
originals={str(q):sha(q.read_bytes()) for q in (root/'media').rglob('*') if q.is_file()}
originals[str(root/'adopted-project.json')]=sha((root/'adopted-project.json').read_bytes())
def retain():(scope/'processes.json').write_text(json.dumps(rows,indent=2)+'\n')
def fixture(mode,executable=None,**extra):
 proc=subprocess.Popen([str((executable or a.context_fixture).resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**extra)),timeout=60)
 except BaseException:proc.kill();out,err=proc.communicate();raise
 prefix=scope/f'{len(rows)}-{mode}';prefix.with_suffix('.stdout').write_text(out);prefix.with_suffix('.stderr').write_text(err)
 rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));retain();assert proc.returncode==0,(scope,err);return json.loads(out)
def run(request,mode='start',historical=None,saved=None,expected=None,seed=None):
 body=request if isinstance(request,str) else json.dumps(request);doc=json.loads(body);op=doc['operation'];packets=[];prefix=scope/f'{len(rows)}-{mode}'
 prefix.with_suffix('.request.json').write_text(body)
 def packet(line):
  j=json.loads(line);packets.append(j)
  if j.get('event')=='ready':
   if mode=='seed':
    fixture('start',executable=a.repeat_fixture,selection=seed['selection'],assembly=seed['assembly'],side=doc['fragment'],ready=j,protected=True,clocked=True);return
   assert j['protocol']=='sc-comp-span-uniform-context-v1' and j['uniformContext']==doc['uniformContext']
   if mode=='late-history':
    changed=json.loads(saved);changed['requestDigest']='0'*64;historical.write_text(json.dumps(changed))
   if mode=='start':(root/f'ready-{j["fragment"]}.json').write_text(json.dumps(j))
   if mode!='wait':(root/'media/comp-spans'/op/('cancel.request' if mode=='cancel' else 'start.request')).write_text('ack\n')
 cmd=[str(a.worker.resolve()),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',expected or sha(body.encode())]
 try:result=observe(cmd,body,packet,timeout=15)
 finally:
  if historical is not None:historical.write_bytes(saved)
 prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
 rows.append(dict(kind='worker',mode=mode,command=cmd,pid=result.pid,exitCode=result.returncode,packets=packets,operation=op,requestFile=str(prefix.with_suffix('.request.json')),retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited));retain()
 assert not result.retirement_pending and not result.observation_timed_out and not result.output_limited,diagnostic(result)
 return result,root/'media/comp-spans'/op
verified=[];failures=[];seedEvidence=None;requestGeometry=[]
for stage in ['nominal','history','short-history']:
 history=stage!='nominal'
 if stage=='short-history':
  seed=fixture('prepare',executable=a.repeat_fixture,generation=0,mixed=False,protected=True,clocked=True,shortUniformFirst=True)
  assert len(seed['requests'])==2
  for req in seed['requests']:
   result,job=run((root/req['file']).read_text(),mode='seed',seed=seed);assert result.returncode==0 and (job/'complete.json').exists(),diagnostic(result)
  seedEvidence=fixture('adopt',executable=a.repeat_fixture,selection=seed['selection'],assembly=seed['assembly'],historicalCompletion=seed['historicalCompletion'],protected=True,clocked=True,lateFailure=False)
  assert all(seedEvidence[k] for k in ['undoRedo','savedReopened','joinedMemoryReleased','queuedStaleApplyRefused','historyFailureBeforePublication','changedHistoryVerificationRefused'])
 requests=fixture('prepare-short-history' if stage=='short-history' else 'prepare-history' if history else 'prepare');assert len(requests)==1
 assert requests[0]['legacyWindowRefused']==(stage=='short-history')
 requestGeometry.append(dict(stage=stage,legacyProcessingFrames=requests[0]['legacyProcessingFrames'],contextInputFrames=requests[0]['contextInputFrames'],outputFrames=requests[0]['outputFrames'],legacyWindowRefused=requests[0]['legacyWindowRefused']))
 body=(root/requests[0]['file']).read_text();request=json.loads(body);assert request['uniformContext']['historyRequired'] is history
 result,job=run(body);assert result.returncode==0 and (job/'complete.json').exists(),diagnostic(result)
 verified.append(fixture('verify'))
 # Existing completed operation is immutable.
 before={name:sha((job/name).read_bytes()) for name in ['audio.wav','complete.json']}
 repeated,_=run(body);assert repeated.returncode!=0 and before=={name:sha((job/name).read_bytes()) for name in before}
 failures.append(dict(mode='repeat-completed',history=history,originalUnchanged=True))
 if stage!='short-history':continue
 historical=root/requests[0]['historicalCompletion'];saved=historical.read_bytes()
 for mode in ['cancel','wait','late-history','early-history']:
  changed=copy.deepcopy(request);changed['operation']=str(uuid.uuid4())
  if mode=='early-history':
   bad=json.loads(saved);bad['requestDigest']='0'*64;historical.write_text(json.dumps(bad))
  result,job=run(changed,mode,historical,saved);assert result.returncode!=0 and not (job/'complete.json').exists(),diagnostic(result)
  assert job.exists()==(mode!='early-history')
  assert any(p.get('event')=='ready' for p in rows[-1]['packets'])==(mode!='early-history')
  failures.append(dict(mode=mode,completionAbsent=True,refusedBeforeOutput=not job.exists()))
 # Each semantic field is rederived even when the outer request hash is recomputed.
 for field,value in request['uniformContext'].items():
  changed=copy.deepcopy(request);changed['operation']=str(uuid.uuid4())
  changed['uniformContext'][field]=(not value) if isinstance(value,bool) else ([value[0]+1]+value[1:]) if isinstance(value,list) else value+1 if isinstance(value,int) else 'incorrect-binding'
  result,job=run(changed);assert result.returncode!=0 and not job.exists(),(field,diagnostic(result));failures.append(dict(mode='context-field',field=field,refusedBeforeOutput=True))
 for mode in ['unknown-field','duplicate-field','wrong-digest','changed-source']:
  changed=copy.deepcopy(request);changed['operation']=str(uuid.uuid4());raw=root/'media/raw.wav';originalRaw=raw.read_bytes()
  if mode=='unknown-field':changed['unrecognized']=True
  body=json.dumps(changed)
  if mode=='duplicate-field':body=body[:-1]+',"fragment":1}'
  if mode=='changed-source':bad=bytearray(originalRaw);bad[-1]^=1;raw.write_bytes(bad)
  try:result,job=run(body,expected='0'*64 if mode=='wrong-digest' else None)
  finally:raw.write_bytes(originalRaw)
  assert result.returncode!=0 and not job.exists(),diagnostic(result);failures.append(dict(mode=mode,refusedBeforeOutput=True))
assert originals=={path:sha(Path(path).read_bytes()) for path in originals}
assert sha(a.worker.read_bytes())==binary
report=dict(workerSha256=binary,actualOriginalHistoryWorkflow=True,successfulContextWorkers=3,verification=verified,requestGeometry=requestGeometry,actualCroppedHistorySetup=seedEvidence,failures=failures,originalMediaAndHistoryUnchanged=True,processes=rows,newPairAdoption=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),successfulWorkers=3,refusals=len(failures),comparedSamples=sum(v['comparedSamples'] for v in verified))))
