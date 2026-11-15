#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual source workers and main-window reviewed Apply with retained evidence."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','controller']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
command=[sys.executable,str(Path(__file__).with_name('comp_pair_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker']]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
processes=[];reports=[]
for root in sorted(roots[0].glob('case-*/project-*')):
    evidence=scope/root.parent.name
    proc=subprocess.Popen([str(a.controller.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),evidence=str(evidence))),timeout=180)
    except BaseException:proc.kill();proc.communicate();raise
    (scope/(root.parent.name+'.stdout')).write_text(out);(scope/(root.parent.name+'.stderr')).write_text(err)
    processes.append(dict(pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err)
    reports.append(json.loads(out))
assert sorted(len(r['cases']) for r in reports)==[1,1,4]
result=dict(inputWorkflow=json.loads((roots[0]/'processes.json').read_text()),controllerProcesses=processes,reports=reports)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),cases=6,controllerProcesses=3)))
