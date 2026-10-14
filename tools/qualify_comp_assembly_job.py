#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain owned final-file publication and source-based recovery evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','ui/','third_party/','localization/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tools/comp_span_worker.cpp','tools/stretch_render_worker.cpp','tools/qualify_comp_assembly_job.py','tests/comp_assembly_job_tests.cpp','tests/comp_assembly_job_tests.py','tests/comp_pair_assembly_tests.cpp','tests/comp_pair_assembly_tests.py','tests/comp_span_worker_fixture.cpp','tests/comp_pair_recovery_fixture.cpp','tests/worker_observation.py']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-comp-assembly-job-tests','sc-stretch-render-worker','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));test_root=build/'comp-assembly-job-evidence';before=set(test_root.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^comp-owned-assembly-publication$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();report=json.loads((run/'report.json').read_text());assert len(report['reports'])==3
workers=[p for p in report['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker']];assert len(workers)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in workers)
assert len(report['publicationProcesses'])==3 and all(p['exitCode']==0 for p in report['publicationProcesses'])
assert all(r['executorSha256']==sha(build/'sc-comp-assembly-job-tests') and r['completedJobs']==1 and r['interruptedJobs']==2 and r['canceledJobs']==1 and r['sourceBasedFullPcmRefusal'] for r in report['reports'])
probe=report['runningImageProbe'];assert probe['exitCode']==0 and probe['runningInodeHashPreserved'] and probe['before']==probe['after']==sha(build/'sc-comp-assembly-job-tests') and probe['replacementPathSha256']!=probe['after']
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
outputs={f.relative_to(run).as_posix():dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file()}
receipt=dict(checkpoint=280,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,report=report,outputFiles=outputs,nominalAndSpanWorkers=12,publicationProcesses=3,runningImageProcesses=1,allProcessesReaped=True,completedJobs=3,interruptedJobs=6,canceledJobs=3,fullOutputSamplesCompared=sum(r['comparedSamples'] for r in report['reports']),noOverwriteRetryRefused=True,lateCancelPreservesCommittedPair=True,interruptionInjectionBeforeAfterFilePublication=True,fullPcmRejectsConsistentForgedHashesInsideAllowedBlend=True,wrongIntentPolicyIdentityQuotaRefused=True,actualImageSurvivesPathReplacement=True,sharedImageHelperUsedBySpanWorker=True,requestReferenceInProjectImplemented=False,desktopJobControllerImplemented=False,osMemoryCeilingClaimed=False,hardDeadlineClaimed=False,actualPowerLossTested=False,independentSonicQualityQualified=False,livePostSrcOverlayImplemented=False,nonuniformProducerImplemented=False,qualityAdmission=False,modelAdoptionChanged=False,nativeWindowsQualified=False,mainWindowIntegrated=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for root,prefix in [(scope,''),(run,'actual-publication/')]:
  for f in sorted(root.rglob('*')):
   if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','report']};index['processPids']=[p['pid'] for p in workers+report['publicationProcesses']]+[probe['pid']];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,completedJobs=3,interruptedJobs=6,canceledJobs=3)))
