#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real timeline creation/review/Apply on successive protected original-history and retained mixed assemblies."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','controller']
for name in names:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
binaries={str(getattr(a,n).resolve()):sha(getattr(a,n).resolve()) for n in names}
command=[sys.executable,str(Path(__file__).with_name('comp_protected_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
rows=[];reports=[]
inputs={str(p):sha(p) for p in roots[0].rglob("*") if p.is_file()}
for root in sorted(roots[0].glob('case-*-protected/project-*')):
    evidence=scope/root.parent.name
    proc=subprocess.Popen([str(a.controller.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),evidence=str(evidence),repeated=True,protected=True)),timeout=360)
    except BaseException:proc.kill();out,err=proc.communicate();(scope/(root.parent.name+'.interrupted.stdout')).write_text(out);(scope/(root.parent.name+'.interrupted.stderr')).write_text(err);raise
    (scope/(root.parent.name+'.stdout')).write_text(out);(scope/(root.parent.name+'.stderr')).write_text(err)
    rows.append(dict(pid=proc.pid,exitCode=proc.returncode));(scope/'processes.json').write_text(json.dumps(rows,indent=2)+'\n');assert proc.returncode==0,(root,err)
    reports.append(json.loads(out))
assert inputs=={str(p):sha(p) for p in roots[0].rglob('*') if p.is_file()}
assert binaries=={path:sha(Path(path)) for path in binaries}
assert all(r['protectedHistory'] and r['actualRepeatedMainWindow'] for r in reports)
assert sorted(len(r['cases']) for r in reports)==[1,1,6]
success=[c for r in reports for c in r['cases'] if c['case']==0]
steps=[g for c in success for g in c['generations']]
assert len(steps)==9 and sum(g['mixedRetained'] for g in steps)==3
assert sum(len(g['children']) for g in steps)==15
assert all(g['protectedSamples']>0 and g['freshOperationIdentities'] and g['currentReviewStatus'] and all(p=='sc-comp-span-protected-v2' for p in g['spanProtocols']) for g in steps)
assert all(g['undoRedoSaveReopen'] and g['noAudioBeforeApply'] and g['unchangedSamples']>0 and g['liveExportSamples']>0 and g['peak']>1 for g in steps)
assert all(c['asynchronousClose'] and c['assemblyCreditsReleased'] for r in reports for c in r['cases'])
failures={c['case']:c['generations'][0] for r in reports for c in r['cases'] if c['case']}
assert all(failures[n]['audioPreserved'] and failures[n]['failureChildrenRetired'] for n in [1,2,3])
assert failures[4]['changedAncestryRefused'] and failures[5]['closeCanceledApply']
result=dict(protectedHistory=True,inputFilesUnchanged=True,binariesUnchanged=True,inputHashes=inputs,binaryHashes=binaries,controllerProcesses=rows,reports=reports,actualRepeatedMainWindow=True,actualAdoptions=9,successfulAdoptionWorkers=15,mixedRetained=3,physicalAudio=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),actualAdoptions=9,mixedRetained=3)))
