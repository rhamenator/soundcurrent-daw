#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import argparse,subprocess,tempfile,json,hashlib,sys,shutil
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[]
command=[sys.executable,str(Path(__file__).with_name('comp_pair_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker']]+['--evidence',str(scope/'inputs')]
with (scope/'input-workflow.stdout').open('w') as out,(scope/'input-workflow.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
inputs=list((scope/'inputs').glob('run-*'));assert len(inputs)==1;root=inputs[0];reports=[]
for project in sorted(root.glob('case-*/project-*')):
    proc=subprocess.Popen([str(a.job_fixture.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(project))),timeout=90)
    except BaseException:proc.kill();proc.communicate();raise
    (project/'job-fixture.stdout').write_text(out);(project/'job-fixture.stderr').write_text(err);rows.append(dict(pid=proc.pid,exitCode=proc.returncode,root=str(project)));assert proc.returncode==0,(project,err);reports.append(json.loads(out))
assert len(reports)==3 and all(r['executorSha256']==hashlib.sha256(a.job_fixture.read_bytes()).hexdigest() for r in reports)
image_probe=None
if sys.platform.startswith('linux'):
    with tempfile.TemporaryDirectory(prefix='sc-running-assembly-image-') as temp:
        image=Path(temp)/'publisher';shutil.copy2(a.job_fixture,image);expected=hashlib.sha256(image.read_bytes()).hexdigest()
        ack=Path(temp)/'replaced';packets=[]
        def ready_image(line):
            packets.append(line)
            if len(packets)==1:
                assert line==expected;image.rename(Path(temp)/'original');image.write_bytes(b'owned replacement path fixture\n');ack.write_text('replaced\n')
        result=observe([str(image),'--image-probe',str(ack)],'',ready_image,timeout=10)
        assert result.returncode==0 and not result.retirement_pending and not result.observation_timed_out and packets==[expected,expected],diagnostic(result)
        image_probe=dict(pid=result.pid,exitCode=result.returncode,originalSha256=expected,before=packets[0],after=packets[1],replacementPathSha256=hashlib.sha256(image.read_bytes()).hexdigest(),runningInodeHashPreserved=True)
result=dict(inputWorkflow=json.loads((root/'processes.json').read_text()),publicationProcesses=rows,reports=reports,runningImageProbe=image_probe,actualPowerLossTested=False,qualityAdmission=False,modelAdoptionChanged=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),publicationProcesses=len(rows),completedJobs=3,interruptedJobs=6,canceledJobs=3)))
