#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind actual worker/assembly evidence to a clean source revision and binaries."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','ui/','third_party/','localization/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/comp_span_worker.cpp','tools/stretch_render_worker.cpp','tools/qualify_comp_pair_assembly.py','tests/comp_pair_assembly_tests.cpp','tests/comp_pair_assembly_tests.py','tests/comp_span_worker_fixture.cpp','tests/comp_pair_recovery_fixture.cpp','tests/worker_observation.py']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-stretch-render-worker','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));test_root=build/'comp-pair-assembly-evidence';before=set(test_root.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^comp-verified-pair-assembly$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();report=json.loads((run/'processes.json').read_text());assert len(report['reports'])==3 and report['workerSha256']==sha(build/'sc-comp-span-worker')
workers=[c for c in report['processes'] if c['kind']=='worker'];assert len(workers)==6 and all(c['exitCode']==0 and not c['retirementPending'] and not c['observationTimedOut'] for c in workers)
nominal=[c for c in report['processes'] if c['kind']=='nominal-worker'];assert len(nominal)==6 and all(c['exitCode']==0 and not c['retirementPending'] and not c['observationTimedOut'] for c in nominal)
assert report['nominalWorkerSha256']==sha(build/'sc-stretch-render-worker') and report['ownedNominalWorkflowQualified'] and not report['nominalBinaryBoundByProtocol']
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
outputs={f.relative_to(run).as_posix():dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file()}
sides=[s for r in report['reports'] for s in r['sides']]
receipt=dict(checkpoint=279,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,report=report,outputFiles=outputs,workerProcesses=12,nominalWorkerProcesses=6,successfulOwnedSpans=6,physicalFragments=2,postSrcFragments=4,exactAssemblySamples=sum(s['exactSamples'] for s in sides),curveSamples=sum(s['blendedSamples'] for s in sides),curveMaximumError=max(s['curveMaximumError'] for s in sides),physicalSamplesCompared=sum(r['physicalSamplesCompared'] for r in report['reports']),projectSamplesCompared=sum(r['projectSamplesCompared'] for r in report['reports']),minimumPeak=min(s['peakLinear'] for s in sides),incompletePairRefused=True,staleContextAndModelRefused=True,missingTrustedReferenceRefused=True,outputAndMemoryGrantsEnforced=True,cancellationAndSinkFailureRefused=True,lateSiblingCompletionChangeRefused=True,partialSinksNotPublished=True,ownedNominalWorkflowQualified=True,nominalCanonicalAdoptionUndoReopen=True,originalNominalReceiptsVerified=True,busyOriginalLeaseRefused=True,nominalPolicyAndBeforeAfterMarkerRefusals=True,nominalBinaryBoundByProtocol=False,independentFirQualityQualified=False,livePostSrcOverlayImplemented=False,nonuniformProducerImplemented=False,qualityAdmission=False,modelAdoptionChanged=False,nativeWindowsQualified=False,mainWindowIntegrated=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,''),(run,'actual-assembly/')]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','report']};index['workerPids']=[r['pid'] for r in nominal+workers];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,workers=len(nominal)+len(workers),successfulSpans=6,physicalFragments=2,postSrcFragments=4)))
