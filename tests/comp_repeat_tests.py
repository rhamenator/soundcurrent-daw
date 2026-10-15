#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual repeated paired production/adoption with retained assembled sources."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','repeat_fixture']
for n in names:p.add_argument(n,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[]
worker=a.worker.resolve();binary=hashlib.sha256(worker.read_bytes()).hexdigest()
command=[sys.executable,str(Path(__file__).with_name('comp_assembly_job_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--evidence',str(scope/'inputs')]
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

for root in sorted(roots[0].glob('inputs/run-*/case-*/project-*')):
    original=(root/'adopted-project.json').read_bytes()
    for generation in range(3):
        prepared=fixture('prepare',root,generation=generation,mixed=generation==2)
        assert len(prepared['requests'])==(1 if generation==2 else 2)
        assert prepared['retainedAssembledCoverageRefused']==(generation==2)
        for request in prepared['requests']:
            ready=[]
            def packet(line):
                data=json.loads(line)
                if data.get('event')=='ready':
                    ready.append(data);fixture('start',root,selection=prepared['selection'],assembly=prepared['assembly'],side=request['side'],ready=data)
            command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
            result=observe(command,(root/request['file']).read_text(encoding='utf-8'),packet,timeout=15)
            prefix=root/f'repeat-{len(rows)}-worker';prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
            rows.append(dict(kind='worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out));retain()
            assert not result.retirement_pending and not result.observation_timed_out and result.returncode==0 and len(ready)==1,diagnostic(result)
        result=fixture('adopt',root,selection=prepared['selection'],assembly=prepared['assembly'],historicalCompletion=prepared['historicalCompletion'],lateFailure=root.parent.name=='case-48000' and generation==0)
        assert result['undoRedo'] and result['savedReopened'] and result['joinedMemoryReleased'] and result['queuedStaleApplyRefused'] and result['historyFailureBeforePublication'] and result['changedHistoryVerificationRefused']
        assert result['historySources']==prepared['historySources']+len(prepared['requests']) and result['unchangedSamples']>0 and result['liveExportSamples']>0 and result['peak']>1
        reports.append(dict(case=root.parent.name,generation=generation,mixedRetained=generation==2,prepared=prepared,result=result))
    assert (root/'adopted-project.json').read_bytes()==original
assert len(reports)==9 and len([r for r in rows if r['kind']=='worker'])==15
assert sum(r['result']['lateHistoryBeforeCompletionRefused'] for r in reports)==1
result=dict(inputWorkflow=json.loads((roots[0]/'report.json').read_text()),workerSha256=binary,processes=rows,reports=reports,actualPairedAdoptions=9,successfulWorkers=15,retainedAssembledMixedCases=3,applicationEntryEnabled=False,physicalAudio=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),actualPairedAdoptions=9,successfulWorkers=15)))
