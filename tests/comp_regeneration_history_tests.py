#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify original rendered jobs from actual adopted project histories."""
from pathlib import Path
import argparse,json,subprocess,sys,tempfile
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','reviewer']
for name in names:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
command=[sys.executable,str(Path(__file__).with_name('comp_assembly_job_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
reports=[];processes=[]
for root in sorted(roots[0].glob('inputs/run-*/case-*/project-*')):
    proc=subprocess.Popen([str(a.reviewer.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root))),timeout=90)
    except BaseException:proc.kill();proc.communicate();raise
    (root/'review.stdout').write_text(out);(root/'review.stderr').write_text(err)
    processes.append(dict(pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);reports.append(json.loads(out))
assert len(reports)==3 and all(len(r['reviews'])==2 and r['projectBytesUnchanged'] and r['joinedMemoryReleased'] and not r['newRendering'] and not r['qualityAdmission'] for r in reports)
assert all(c['assemblies']==1 and c['comparedSamples']>0 for r in reports for c in r['reviews'])
result=dict(inputs=json.loads((roots[0]/'report.json').read_text()),reviewProcesses=processes,reports=reports)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),reviews=6,checks=sum(r['checks'] for r in reports))))
