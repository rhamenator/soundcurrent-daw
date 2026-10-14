#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual isolated span workers followed by permission-bound streamed assembly."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();worker=a.worker.resolve();binary=hashlib.sha256(worker.read_bytes()).hexdigest();a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[]
def driver(exe,mode,root,**data):
    proc=subprocess.Popen([str(exe.resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**data)),timeout=30)
    except BaseException:
        proc.kill();proc.communicate();raise
    prefix=f'{len(rows)}-{mode}';(root/(prefix+'.stdout')).write_text(out);(root/(prefix+'.stderr')).write_text(err);rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);return json.loads(out)
for rate,physical in [(48000,True),(44100,False),(96000,False)]:
    root=scope/f'case-{rate}'/'project-é-日本';root.mkdir(parents=True)
    driver(a.span_fixture,'prepare',root,rate=rate,pitch=700007)
    requests=driver(a.assembly_fixture,'prepare',root,physical=physical)
    driver(a.pair_fixture,'prepare',root);driver(a.assembly_fixture,'unready',root)
    for i,request in enumerate(requests):
        def ready(line):
            packet=json.loads(line)
            if packet.get('event')=='ready':driver(a.pair_fixture,'start',root,ready=packet)
        command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
        result=observe(command,(root/request['file']).read_text(),ready,timeout=15);prefix=f'{len(rows)}-worker';(root/(prefix+'.stdout')).write_text(result.stdout);(root/(prefix+'.stderr')).write_text(result.stderr)
        rows.append(dict(kind='worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out))
        assert result.returncode==0 and not result.retirement_pending and not result.observation_timed_out,diagnostic(result)
        if i==0:driver(a.assembly_fixture,'unready',root)
    report=driver(a.assembly_fixture,'verify',root);reports.append(report)
    assert all(side['domain']==(1 if physical else 2) for side in report['sides'])
    assert not report['nominalProducerQualified'] and not report['qualityAdmission'] and not report['modelAdoptionChanged']
result=dict(workerSha256=binary,processes=rows,reports=reports,completedWorkers=6,physicalFragments=2,postSrcFragments=4,nominalProducerQualified=False,qualityAdmission=False,modelAdoptionChanged=False)
(scope/'processes.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),completedWorkers=6,physicalFragments=2,postSrcFragments=4)))
