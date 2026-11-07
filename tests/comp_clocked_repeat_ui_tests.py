#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual desktop clocked protected, post-SRC and retained mixed comp workflows."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
p=argparse.ArgumentParser()
names=['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','controller']
for name in names:p.add_argument(name,type=Path)
p.add_argument('--kind',choices=['protected','uniform'],required=True)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
binaries={str(getattr(a,n).resolve()):sha(getattr(a,n).resolve()) for n in names}
command=[sys.executable,str(Path(__file__).with_name('comp_protected_assembly_tests.py'))]+[str(getattr(a,n).resolve()) for n in names[:-1]]+['--case','48000-'+a.kind,'--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
rows=[];reports=[]
inputs={str(p):sha(p) for p in roots[0].rglob("*") if p.is_file()}
for root in sorted(roots[0].glob('case-*/project-*')):
    evidence=scope/root.parent.name
    proc=subprocess.Popen([str(a.controller.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),evidence=str(evidence),repeated=True,protected=True,clocked=True,extendedFailures=a.kind=='protected')),timeout=360)
    except BaseException:proc.kill();out,err=proc.communicate();(scope/(root.parent.name+'.interrupted.stdout')).write_text(out);(scope/(root.parent.name+'.interrupted.stderr')).write_text(err);raise
    (scope/(root.parent.name+'.stdout')).write_text(out);(scope/(root.parent.name+'.stderr')).write_text(err)
    rows.append(dict(pid=proc.pid,exitCode=proc.returncode));(scope/'processes.json').write_text(json.dumps(rows,indent=2)+'\n');assert proc.returncode==0,(root,err)
    reports.append(json.loads(out))
assert inputs=={str(p):sha(p) for p in roots[0].rglob('*') if p.is_file()}
assert binaries=={path:sha(Path(path)) for path in binaries}
assert all(r['protectedHistory'] and r['actualRepeatedMainWindow'] for r in reports)
assert len(reports)==1 and len(reports[0]['cases'])==(6 if a.kind=='protected' else 1)
assert reports[0]['clockedProtected']
success=[c for r in reports for c in r['cases'] if c['case']==0]
steps=[g for c in success for g in c['generations']]
assert len(steps)==3 and sum(g['mixedRetained'] for g in steps)==1
assert sum(len(g['children']) for g in steps)==5
expected=['sc-comp-span-protected-clocked-v1','sc-comp-span-protected-clocked-v1' if a.kind=='protected' else 'sc-comp-span-uniform-context-v1']
assert all(g['clockedSamples']>0 and g['freshOperationIdentities'] and g['currentReviewStatus'] and g['pairFormat']==('sc-comp-pair-selection-clocked-protected-v1' if a.kind=='protected' else 'sc-comp-pair-selection-original-context-v1') for g in steps)
assert [g['spanProtocols'] for g in steps]==[expected,expected,expected[1:]]
assert [g['speedControlQualified'] for g in steps]==[True,True,False]
assert all(g['undoRedoSaveReopen'] and g['noAudioBeforeApply'] and g['unchangedSamples']>0 and g['liveExportSamples']>0 and g['peak']>1 for g in steps)
assert all(c['asynchronousClose'] and c['assemblyCreditsReleased'] for r in reports for c in r['cases'])
failures={c['case']:c['generations'][0] for r in reports for c in r['cases'] if c['case']}
if a.kind=='protected':
    assert all(failures[n]['audioPreserved'] and failures[n]['failureChildrenRetired'] for n in [1,2,3])
    assert failures[4]['changedAncestryRefused'] and failures[5]['closeCanceledApply']
else:assert not failures
result=dict(protectedHistory=True,inputFilesUnchanged=True,binariesUnchanged=True,inputHashes=inputs,binaryHashes=binaries,controllerProcesses=rows,reports=reports,actualRepeatedMainWindow=True,actualAdoptions=3,successfulAdoptionWorkers=5,mixedRetained=1,clockedProtected=True,caseKind=a.kind,physicalAudio=False,qualityAdmission=False)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),actualAdoptions=3,mixedRetained=1)))
