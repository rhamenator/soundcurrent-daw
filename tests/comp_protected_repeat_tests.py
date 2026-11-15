#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual protected-history regeneration, adoption and retained assembled sources."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','repeat_fixture']
for n in names:p.add_argument(n,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[];failures=[]
worker=a.worker.resolve();binary=hashlib.sha256(worker.read_bytes()).hexdigest()
command=[sys.executable,str(Path(__file__).with_name('comp_protected_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1

def retain(): (scope/'processes.json').write_text(json.dumps(rows,indent=2)+'\n')
def fixture(mode,root,**data):
    proc=subprocess.Popen([str(a.repeat_fixture.resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**data)),timeout=180)
    except BaseException:proc.kill();proc.communicate();raise
    prefix=root/f'repeat-{len(rows)}-{mode}';prefix.with_suffix('.stdout').write_text(out);prefix.with_suffix('.stderr').write_text(err)
    rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));retain();assert proc.returncode==0,(root,proc.returncode,err);return json.loads(out)

for root in sorted(roots[0].glob('case-*-protected/project-*')):
    original=(root/'adopted-project.json').read_bytes()
    if root.parent.name=='case-48000-protected':
        for failure in ['cancel','late-history']:
            prepared=fixture('prepare',root,generation=0,mixed=False,protected=True)
            request=prepared['requests'][0];marker=root/prepared['historicalCompletion'];saved=marker.read_bytes();ready=[]
            def packet(line):
                data=json.loads(line)
                if data.get('event')!='ready':return
                ready.append(data)
                if failure=='cancel':fixture('cancel',root,selection=prepared['selection'],assembly=prepared['assembly'])
                else:
                    changed=json.loads(saved);changed['requestDigest']='0'*64;marker.write_text(json.dumps(changed))
                    fixture('start',root,selection=prepared['selection'],assembly=prepared['assembly'],side=request['side'],ready=data)
            command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
            try:result=observe(command,(root/request['file']).read_text(),packet,timeout=15)
            finally:marker.write_bytes(saved)
            prefix=root/f'failure-{failure}';prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
            rows.append(dict(kind='refused-worker',failure=failure,pid=result.pid,exitCode=result.returncode,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited));retain()
            assert result.returncode==1 and len(ready)==1 and not result.retirement_pending and not result.observation_timed_out and not result.output_limited,diagnostic(result)
            assert not (root/'media/comp-spans'/request['operation']/'complete.json').exists()
            recovered=fixture('inspect',root,selection=prepared['selection'],assembly=prepared['assembly'])
            assert not recovered['candidatesReady'] and recovered['states'][0]==(5 if failure=='cancel' else 4)
            failures.append(dict(kind=failure,selection=prepared['selection'],operation=request['operation'],recovery=recovered,completionAbsent=True))
    for generation in range(3):
        prepared=fixture('prepare',root,generation=generation,mixed=generation==2,protected=True)
        assert len(prepared['requests'])==(1 if generation==2 else 2)
        assert prepared['retainedAssembledCoverageRefused']==(generation==2)
        assert all(r['protocol']=='sc-comp-span-protected-v2' for r in prepared['requests'])
        for request in prepared['requests']:
            ready=[]
            def packet(line):
                data=json.loads(line)
                if data.get('event')=='ready':
                    ready.append(data);fixture('start',root,selection=prepared['selection'],assembly=prepared['assembly'],side=request['side'],ready=data)
            command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
            result=observe(command,(root/request['file']).read_text(encoding='utf-8'),packet,timeout=15)
            prefix=root/f'repeat-{len(rows)}-worker';prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
            rows.append(dict(kind='worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited));retain()
            assert not result.retirement_pending and not result.observation_timed_out and not result.output_limited and result.returncode==0 and len(ready)==1,diagnostic(result)
        result=fixture('adopt',root,selection=prepared['selection'],assembly=prepared['assembly'],historicalCompletion=prepared['historicalCompletion'],protected=True,lateFailure=root.parent.name=='case-48000-protected' and generation==0)
        assert result['undoRedo'] and result['savedReopened'] and result['joinedMemoryReleased'] and result['queuedStaleApplyRefused'] and result['historyFailureBeforePublication'] and result['changedHistoryVerificationRefused']
        assert result['protectedSamples']>0
        assert result['historySources']==prepared['historySources']+len(prepared['requests']) and result['unchangedSamples']>0 and result['liveExportSamples']>0 and result['peak']>1
        reports.append(dict(case=root.parent.name,generation=generation,mixedRetained=generation==2,prepared=prepared,result=result))
    assert (root/'adopted-project.json').read_bytes()==original
assert len(failures)==2
assert len(reports)==9 and len([r for r in rows if r['kind']=='worker'])==15
assert sum(r['result']['lateHistoryBeforeCompletionRefused'] for r in reports)==1
result=dict(inputWorkflow=json.loads((roots[0]/'report.json').read_text()),workerSha256=binary,processes=rows,reports=reports,failures=failures,actualPairedAdoptions=9,successfulWorkers=15,retainedAssembledMixedCases=3,applicationEntryEnabled=True,physicalAudio=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),actualPairedAdoptions=9,successfulWorkers=15)))
