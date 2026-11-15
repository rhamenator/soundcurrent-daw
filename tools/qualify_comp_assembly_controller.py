#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze actual desktop assembly jobs, copied projects and source identities."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','ui/','third_party/','localization/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/comp_span_worker.cpp','tools/qualify_comp_assembly_controller.py','tests/comp_assembly_controller_tests.cpp','tests/comp_assembly_controller_tests.py','tests/comp_pair_assembly_tests.cpp','tests/comp_pair_assembly_tests.py','tools/stretch_render_worker.cpp','tests/worker_observation.py','tests/comp_span_worker_fixture.cpp','tests/comp_pair_recovery_fixture.cpp']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker','sc-comp-assembly-controller-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));test_root=build/'comp-assembly-controller-evidence';test_root.mkdir(exist_ok=True);before=set(test_root.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^desktop-comp-assembly-ownership$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();report=json.loads((run/'report.json').read_text())
assert len(report['controllerProcesses'])==3 and all(p['exitCode']==0 for p in report['controllerProcesses'])
cases=[case for r in report['reports'] for case in r['cases']];assert len(cases)==15
assert all(r['executorSha256']==sha(build/'sc-comp-assembly-controller-tests') for r in report['reports'])
assert all(case['joinedMemoryReleased'] and case['closed']['closed'] for case in cases)
workers=[p for p in report['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker']]
assert len(workers)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in workers)
normal=[c for c in cases if c['case']==0];assert len(normal)==3
assert all(c['terminal']['published'] and c['terminal']['samples']>0 and not c['review']['published'] and not c['reuse']['published'] for c in normal)
expanded=next(r['cases'] for r in report['reports'] if len(r['cases'])==13)
assert all(expanded[i]['terminal']['stale'] for i in [2,4,5])
assert all(expanded[i]['terminal']['canceled'] for i in [1,3,8,12])
assert expanded[8]['terminal']['published'] and expanded[8]['review']['samples']>0
assert all(not expanded[i]['terminal']['published'] and expanded[i]['terminal']['error'] is not None for i in [6,7,9,10,11])
assert all(not r['mainWindowIntegrated'] and not r['modelAudioAdoption'] and not r['osIsolation'] and not r['hardDeadline'] for r in report['reports'])
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
outputs={f.relative_to(run).as_posix():dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file()}
receipt=dict(checkpoint=282,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,report=report,outputFiles=outputs,cases=15,sourceWorkers=12,controllerProcesses=3,allProcessesReaped=True,liveProjectControllerUsed=True,joinedMemoryReleasedAfterRetirement=True,sameRootReopenAndStaleEditsRefused=True,unsavedReferenceRefused=True,changedRequestAndWrongExecutableRefused=True,guiResponsiveDuringPublication=True,asynchronousClose=True,durableCancellation=True,lateCancellationPreservesCompletedPair=True,readOnlyRecoveryAndReuse=True,canonicalAudioAdoption=False,mainWindowIntegrated=False,osIsolation=False,hardDeadline=False,postSrcLiveOverlay=False,qualityAdmission=False,nativeWindowsQualified=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,''),(run,'actual-controller/')]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','report']};index['workerPids']=[r['pid'] for r in workers];index['controllerPids']=[r['pid'] for r in report['controllerProcesses']];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,cases=15)))
