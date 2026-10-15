#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real version-two span workers over saved, actually adopted source histories."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','v2_fixture']
for n in names:p.add_argument(n,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[];failures=[]
worker=a.worker.resolve();binary=hashlib.sha256(worker.read_bytes()).hexdigest()
command=[sys.executable,str(Path(__file__).with_name('comp_assembly_job_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
def fixture(mode,root,**data):
    proc=subprocess.Popen([str(a.v2_fixture.resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**data)),timeout=30)
    except BaseException:proc.kill();proc.communicate();raise
    prefix=root/f'v2-{len(rows)}-{mode}';prefix.with_suffix('.stdout').write_text(out);prefix.with_suffix('.stderr').write_text(err)
    rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);return json.loads(out)
def run_worker(root,request,on_ready):
    def packet(line):
        data=json.loads(line)
        if data.get('event')=='ready':on_ready(data)
    command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
    result=observe(command,(root/request['file']).read_text(encoding='utf-8'),packet,timeout=15)
    prefix=root/f'v2-{len(rows)}-worker';prefix.with_suffix('.stdout').write_text(result.stdout);prefix.with_suffix('.stderr').write_text(result.stderr)
    rows.append(dict(kind='v2-worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out))
    assert not result.retirement_pending and not result.observation_timed_out,diagnostic(result)
    return result
for root in sorted(roots[0].glob('inputs/run-*/case-*/project-*')):
    project=(root/'project.json').read_bytes();adopted=(root/'adopted-project.json').read_bytes()
    for mixed in [False,True]:
        prepared=fixture('prepare',root,label='mixed' if mixed else 'paired',mixed=mixed);assert prepared['projectUnchanged'] and len(prepared['requests'])==(1 if mixed else 2)
        for request in prepared['requests']:
            ready=[]
            def start(packet):
                ready.append(packet);fixture('ready',root,file=request['file'],digest=request['digest'],ready=packet)
            result=run_worker(root,request,start);assert result.returncode==0 and len(ready)==1,diagnostic(result)
            complete=json.loads(result.stdout.splitlines()[-1]);assert complete['protocol']=='sc-comp-span-v2' and complete['complete'] and complete['rate']==request['outputRate'] and complete['clock']['inputRate']==request['sourceRate'] and complete['clock']['outputRate']==request['outputRate']
            report=fixture('verify',root,file=request['file'],digest=request['digest']);assert report['historyReviewed'] and report['projectUnchanged'] and report['joinedMemoryReleased'] and not report['assemblyAdoption'] and not report['qualityAdmission'];reports.append(dict(mixed=mixed,request=request,completion=complete,verification=report))
    if root.parent.name=='case-48000':
        for label in ['missing-history','late-history','mixed-history','cancel']:
            prepared=fixture('prepare',root,label=label,mixed=label=='mixed-history');request=prepared['requests'][0]
            marker=root/prepared['historicalCompletion'];saved=marker.read_bytes();parked=marker.with_name('complete.v2-test-parked');ready=[]
            job=root/'media/comp-spans'/request['operation'];assert not job.exists()
            if label in ['missing-history','mixed-history']:
                assert not parked.exists();marker.rename(parked)
            def start_failure(packet):
                ready.append(packet)
                if label=='late-history':
                    changed=json.loads(saved);changed['requestDigest']='0'*64;bad=json.dumps(changed).encode();marker.write_bytes(bad);(root/'v2-late-history-injected.json').write_bytes(bad)
                    fixture('ready',root,file=request['file'],digest=request['digest'],ready=packet)
                elif label=='cancel':(job/'cancel.request').write_text('cancel\n')
                else:raise AssertionError('Missing history was accepted before readiness')
            try:result=run_worker(root,request,start_failure)
            finally:
                if parked.exists():parked.rename(marker)
                else:marker.write_bytes(saved)
            assert marker.read_bytes()==saved
            assert result.returncode==1 and not (job/'complete.json').exists(),diagnostic(result)
            errors=[json.loads(line) for line in result.stderr.splitlines() if line.startswith('{')];assert errors and errors[-1]['protocol']=='sc-comp-span-v2' and not errors[-1]['publicationMayHaveCommitted']
            if label in ['missing-history','mixed-history']:assert not ready and not job.exists()
            else:assert len(ready)==1
            if label=='late-history':assert (job/'audio.partial').is_file() and not (job/'audio.wav').exists()
            failures.append(dict(case=label,ready=bool(ready),completionAbsent=True,historicalMarkerRestored=True,error=errors[-1]))
    assert (root/'project.json').read_bytes()==project and (root/'adopted-project.json').read_bytes()==adopted
assert len(reports)==9 and len(failures)==4
assert {r['request']['outputRate'] for r in reports}=={44100,48000,96000}
result=dict(inputWorkflow=json.loads((roots[0]/'report.json').read_text()),workerSha256=binary,processes=rows,reports=reports,failures=failures,successfulV2Workers=9,refusedV2Workers=4,completePcmSamples=sum(r['verification']['comparedSamples'] for r in reports),projectBytesUnchanged=True,applicationEntryEnabled=False,pairedAssemblyExercised=False,qualityAdmission=False,physicalAudio=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),successfulV2Workers=9,refusedV2Workers=4,comparedSamples=result['completePcmSamples'])))
