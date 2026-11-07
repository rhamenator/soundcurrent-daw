#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clocked worker on actual published/adopted legacy history, not mock journals."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile,uuid,copy
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','clocked_fixture']
for name in names:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();sha=lambda b:hashlib.sha256(b).hexdigest();binary=sha(a.worker.read_bytes());rows=[]
command=[sys.executable,str(Path(__file__).with_name('comp_protected_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--case','48000-protected','--evidence',str(scope/'input')]
with (scope/'input.stdout').open('w') as out,(scope/'input.stderr').open('w') as err:
 result=subprocess.run(command,stdout=out,stderr=err);assert result.returncode==0,scope
root=next((scope/'input').glob('run-*/case-48000-protected/project-*'))
originals={str(q):sha(q.read_bytes()) for q in root.rglob('*') if q.is_file()}
def retain(): (scope/'processes.json').write_text(json.dumps(rows,indent=2)+'\n')
def fixture(mode,**extra):
 proc=subprocess.Popen([str(a.clocked_fixture.resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**extra)),timeout=60)
 except BaseException:proc.kill();out,err=proc.communicate();raise
 prefix=scope/f'{len(rows)}-{mode}';prefix.with_suffix('.stdout').write_text(out);prefix.with_suffix('.stderr').write_text(err)
 rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));retain();assert proc.returncode==0,(scope,err);return json.loads(out)
requests=fixture('prepare-history');assert len(requests)==2
historical=root/requests[0]['historicalCompletion'];saved=historical.read_bytes()
def run(request,mode='start'):
 body=request if isinstance(request,str) else json.dumps(request);op=json.loads(body)['operation'];packets=[]
 def packet(line):
  j=json.loads(line);packets.append(j)
  if j.get('event')=='ready':
   assert j['protocol']=='sc-comp-span-protected-clocked-v1' and j['clock']['historyRequired'] is True
   if mode=='late-history':
    changed=json.loads(saved);changed['requestDigest']='0'*64;historical.write_text(json.dumps(changed))
   if mode=='start':(root/f'ready-{j["fragment"]}.json').write_text(json.dumps(j))
   (root/'media/comp-spans'/op/('cancel.request' if mode=='cancel' else 'start.request')).write_text('ack\n')
 cmd=[str(a.worker.resolve()),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',sha(body.encode())]
 try:result=observe(cmd,body,packet,timeout=15)
 finally:historical.write_bytes(saved)
 prefix=scope/f'{len(rows)}-{mode}';prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
 rows.append(dict(kind='worker',mode=mode,pid=result.pid,exitCode=result.returncode,packets=packets,operation=op,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited));retain()
 assert not result.retirement_pending and not result.observation_timed_out and not result.output_limited,diagnostic(result)
 return result,root/'media/comp-spans'/op
for request in requests:
 result,job=run((root/request['file']).read_text());assert result.returncode==0 and (job/'complete.json').exists(),diagnostic(result)
verified=fixture('verify')
original=json.loads((root/requests[0]['file']).read_text());failures=[]
for mode in ['cancel','late-history','early-history','wrong-clock']:
 request=copy.deepcopy(original);request['operation']=str(uuid.uuid4())
 if mode=='early-history':
  changed=json.loads(saved);changed['requestDigest']='0'*64;historical.write_text(json.dumps(changed))
 if mode=='wrong-clock':request['clock']['historyRequired']=False
 result,job=run(request,mode);assert result.returncode!=0 and not (job/'complete.json').exists(),diagnostic(result)
 assert job.exists()==(mode in ['cancel','late-history'])
 failures.append(dict(mode=mode,completionAbsent=True,refusedBeforeOutput=not job.exists()))
assert originals=={path:sha(Path(path).read_bytes()) for path in originals}
assert sha(a.worker.read_bytes())==binary
report=dict(workerSha256=binary,actualOriginalHistoryWorkflow=True,successfulClockedWorkers=2,verification=verified,failures=failures,originalInputsUnchanged=True,processes=rows,newPairAdoption=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),successfulWorkers=2,refusals=len(failures),comparedSamples=verified['comparedSamples'])))
