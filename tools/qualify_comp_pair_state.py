#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze actual project-owned paired selection recovery, files, identities and failure evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','third_party/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/comp_span_worker.cpp','tools/qualify_comp_span_worker.py','tests/comp_span_worker_fixture.cpp','tests/comp_span_worker_tests.py','tests/worker_observation.py','tests/comp_pair_recovery_fixture.cpp','tests/comp_pair_recovery_tests.py','tools/qualify_comp_pair_recovery.py','tools/qualify_comp_pair_state.py','tests/comp_pair_state_tests.cpp','tests/session_tests.cpp','tests/project_schema_fixture.hpp','tests/comp_window_state_tests.cpp','tests/history_resource_tests.cpp','tests/editing_tests.cpp']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-state-tests','sc-session-tests','sc-comp-window-state-tests','sc-history-resource-tests','sc-editing-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));test_root=build/'comp-pair-recovery-evidence';before=set(test_root.iterdir());state_root=build/'comp-pair-state-evidence';state_root.mkdir(exist_ok=True);state_before=set(state_root.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-paired-project-state|comp-paired-selection-recovery|session-state|comp-processed-window-state|history-resources|multitrack-edits)$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();processes=json.loads((run/'processes.json').read_text());workers=[r for r in processes['processes'] if r['kind']=='worker'];success=[r for r in workers if r['exitCode']==0]
assert len(success)==2 and processes['workerSha256']==sha(build/'sc-comp-span-worker') and not processes['qualityAdmission'];assert any(r['action']=='wait' and r['exitCode']==2 for r in workers)
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
state_new=set(state_root.iterdir())-state_before;assert len(state_new)==1;state_run=state_new.pop()
output_files={f.relative_to(run).as_posix():dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file()}
receipt=dict(checkpoint=276,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,processes=processes,outputFiles=output_files,workerProcesses=len(workers),successfulOwnedSpans=2,refusedWorkers=len(workers)-2,sourceFilesAdmitted=True,actualExecutableIdentityChecked=True,independentCompletionAndPcmVerified=True,activeChildRefused=True,pairedSelectionReopened=True,trustedReferenceInProjectState=True,projectReferenceUndoRedo=True,projectRelocationTested=True,staleSelectionRejected=True,retiredStartRejected=True,syntheticInterruptedStartInspected=True,allExistingMediaPairInspected=True,deadlineWatchdogObserved=True,parentControllerIntegrated=False,nonuniformProducerImplemented=False,curveAlignmentQualified=False,postSrcOverlayImplemented=False,qualityAdmission=False,modelAdoptionChanged=False,nativeWindowsQualified=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,''),(run,'actual-worker/'),(state_run,'actual-project-state/')]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','processes']};index['workerPids']=[r['pid'] for r in workers];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,workers=len(workers),successfulSpans=2)))
