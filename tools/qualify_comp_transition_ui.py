#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain source-bound timeline creation, Apply and controller regression evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for n in ['build','evidence','public']:p.add_argument(n,type=Path)
a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','ui/','third_party/','localization/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/qualify_comp_transition_ui.py','tools/qualify_comp_assembly_ui.py','tests/comp_transition_ui_tests.cpp','tests/comp_transition_ui_tests.py','tools/comp_span_worker.cpp','tools/stretch_render_worker.cpp','tests/comp_assembly_ui_tests.cpp','tests/comp_assembly_ui_tests.py','tests/comp_assembly_controller_tests.cpp','tests/comp_assembly_controller_tests.py','tests/comp_pair_assembly_tests.cpp','tests/comp_pair_assembly_tests.py','tests/worker_observation.py','tests/comp_span_worker_fixture.cpp','tests/comp_pair_recovery_fixture.cpp']]
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-comp-transition-ui-tests','soundcurrent-daw','sc-comp-assembly-ui-tests','sc-comp-assembly-controller-tests','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker']]+sorted(build.glob('lib*.a'))
binaries={str(n):sha(n) for n in inputs};evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));roots=[build/'comp-assembly-ui-evidence',build/'comp-assembly-controller-evidence',build/'comp-transition-ui-evidence']
for r in roots:r.mkdir(exist_ok=True)
before=[set(r.iterdir()) for r in roots]
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(desktop-comp-assembly-(apply|ownership)|desktop-comp-transition-create|desktop-paired-crossfade|windows-package-inputs)$']
start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
runs=[];reports=[]
for r,b in zip(roots,before):
 new=set(r.iterdir())-b;assert len(new)==1
 run=new.pop();runs.append(run);reports.append(json.loads((run/'report.json').read_text()))
ui,controller,creation=reports;cases=[c for r in ui['reports'] for c in r['cases']]
assert len(cases)==6 and all(c['asynchronousClose'] for c in cases)
assert all(r['mainWindowIntegrated'] and not r['physicalAudio'] and not r['qualityAdmission'] and r['executorSha256']==sha(build/'sc-comp-assembly-ui-tests') for r in ui['reports'])
normal=[c for c in cases if c['case']==0]
assert len(normal)==3 and all(c['undoRedoSaveReopen'] and c['rawAndForeignLedgerRefused'] and c['exportPeak']>1 for c in normal)
previous=repo/'tests/results/M2/2026-10-14-comp-playback-state/acceptance.zip'
with zipfile.ZipFile(previous) as z:baseline=json.loads(z.read('qualification.json'))
assert baseline['sourceHead']=='c98af977dedcdace1dc07820312a78d6d8a0c5c9'
assert [c['exportFileSha256'] for c in normal]==[r['adoption']['exportFileSha256'] for r in baseline['report']['reports']]

expanded=next(r['cases'] for r in ui['reports'] if len(r['cases'])==4)
assert expanded[1]['changedFileRefused'] and expanded[2]['sameRootReopenRefused'] and expanded[3]['closeCanceledWithoutEdit'] and expanded[3]['guiTicks']>0
controllers=[c for r in controller['reports'] for c in r['cases']]
assert len(controllers)==15 and all(c['joinedMemoryReleased'] and c['closed']['closed'] for c in controllers)
assert all(r['executorSha256']==sha(build/'sc-comp-assembly-controller-tests') for r in controller['reports'])
created=[c for r in creation['reports'] for c in r['cases']]
assert len(created)==8 and all(c['asynchronousClose'] for c in created)
normal_created=[c for c in created if c['case']==0]
assert len(normal_created)==3 and all(c['actualTimelineDialog'] and c['noAudioBeforeApply'] and c['undoRedoSaveReopen'] for c in normal_created)
assert [c['exportFileSha256'] for c in normal_created]==[c['exportFileSha256'] for c in normal]
assert all(r['actualNewJobCreation'] and not r['physicalAudio'] and not r['qualityAdmission'] and r['executorSha256']==sha(build/'sc-comp-transition-ui-tests') for r in creation['reports'])
new_children=[p for c in normal_created for p in c['newChildren']]
assert len(new_children)==6 and all(p['pid']>0 and p['exitCode']==0 and p['reaped'] for p in new_children)
failures=[c for c in created if c['case']!=0]
assert len(failures)==5 and all(c['noChildLaunch'] for c in failures)
assert all(c['guiTicks']>0 for c in failures if c['case']<=3)
assert next(c for c in failures if c['case']==5)['saveFailureRetainsAtomicRefsWithoutAudio']
processes=list(new_children)
for report in reports:
 workers=[p for p in report['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker']]
 assert len(workers)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in workers)
 assert len(report['controllerProcesses'])==3 and all(p['exitCode']==0 for p in report['controllerProcesses'])
 processes+=workers+report['controllerProcesses']
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=287,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,reports=reports,processes=processes,allProcessesReaped=True,mainWindowCases=6,controllerCases=15,actualUndoRedoSaveReopenExport=True,exportsMatchPriorEngineQualification=True,priorEngineAcceptanceSha256=sha(previous),rawAndForeignLedgerRefused=True,freshFileVerificationOnApply=True,sameRootReopenRefused=True,closeCancelsPendingApply=True,guiResponsiveDuringVerification=True,asynchronousClose=True,nativeWindowsQualified=False,physicalAudio=False,qualityAdmission=False,newTransitionCreationUi=True,newTransitionCases=8,freshRenderedChildren=6,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,'')]+[(r,['main-window/','controller/','creation/'][i]) for i,r in enumerate(runs)]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','reports']}
index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),runs=[str(r) for r in runs],archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed)))
