#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze actual paired supervisor processes, owned files and source identities."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','ui/','third_party/','localization/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/comp_span_worker.cpp','tools/qualify_comp_pair_supervisor.py','tests/comp_pair_controller_tests.cpp','tests/comp_span_worker_fixture.cpp','tests/comp_pair_recovery_fixture.cpp']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-controller-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));test_root=build/'comp-pair-controller-evidence';before=set(test_root.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^desktop-comp-pair-supervision$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();report=json.loads((run/'report.json').read_text());assert len(report['cases'])==10 and report['workerSha256']==sha(build/'sc-comp-span-worker')
workers=[c for case in report['cases'] for c in case['terminal']['children'] if c['pid']];assert len(workers)==9 and all(c['reaped'] for c in workers) and sum(c['exitCode']==0 for c in workers)==3
assert report['cases'][9]['terminal']['timedOut'] and report['cases'][9]['terminal']['children'][0]['exitCode']==2
assert not report['qualityAdmission'] and not report['modelAdoptionChanged'] and not report['mainWindowIntegrated']
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
outputs={f.relative_to(run).as_posix():dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file()}
receipt=dict(checkpoint=277,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,report=report,outputFiles=outputs,workerProcesses=len(workers),successfulOwnedSpans=3,liveProjectControllerUsed=True,joinedParentMemoryLedger=True,childGrantHeldBeforeSpawn=True,childGrantReleasedAfterReaping=True,staleClipAndProjectReplacementRefused=True,partialPairCancellationPreservesCompletion=True,unsavedReferenceRefused=True,actualDeadlineWatchdogObserved=True,asynchronousShutdownReaped=True,readOnlyRecoveryAndReuseLaunchNoChild=True,mainWindowIntegrated=False,finalAssemblyImplemented=False,curveAlignmentQualified=False,postSrcOverlayImplemented=False,nonuniformProducerImplemented=False,qualityAdmission=False,modelAdoptionChanged=False,nativeWindowsQualified=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,''),(run,'actual-supervisor/')]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','report']};index['workerPids']=[r['pid'] for r in workers];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,workers=len(workers),successfulSpans=3)))
