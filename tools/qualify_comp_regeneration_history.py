#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain source-bound original-history verification and recipe compatibility."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for n in ['build','evidence','public']:p.add_argument(n,type=Path)
a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if (n.startswith(('include/','src/','ui/','third_party/','tests/','tools/','.github/')) and not n.startswith('tests/results/')) or n=='CMakeLists.txt']
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-comp-regeneration-history-tests','sc-comp-regeneration-source-tests','sc-comp-processing-recipe-tests','sc-comp-assembly-job-tests','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker']]+sorted(build.glob('lib*.a'))
binaries={str(n):sha(n) for n in inputs};evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
runs=[build/'comp-regeneration-history-evidence',build/'comp-regeneration-source-evidence']
for r in runs:r.mkdir(exist_ok=True)
before=[set(r.iterdir()) for r in runs]
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-regeneration-history|comp-regeneration-source|comp-processing-recipe-recovery)$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
seconds=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=[set(r.iterdir())-old for r,old in zip(runs,before)];assert all(len(s)==1 for s in new);actual=[s.pop() for s in new]
reports=[json.loads((r/'report.json').read_text()) for r in actual];history,plans=reports
assert len(history['reports'])==len(plans['reports'])==3
assert all(len(r['reviews'])==2 and r['projectBytesUnchanged'] and r['joinedMemoryReleased'] and not r['newRendering'] and not r['qualityAdmission'] for r in history['reports'])
assert all(c['assemblies']==1 and c['comparedSamples']>0 for r in history['reports'] for c in r['reviews'])
assert all(r['plannedRecipes']==4 and r['legacyRecipes']==2 and r['mixedAdmissionRefused'] and not r['fileVerification'] for r in plans['reports'])
assert all(p['exitCode']==0 for p in history['reviewProcesses']+plans['resolverProcesses'])
workers=[]
for r in reports:
    parent=r['inputs'];assert all(p['exitCode']==0 for p in parent['publicationProcesses'])
    assert all(x['adoption']['undoRedo'] and x['adoption']['savedReopened'] and x['adoption']['liveEqualsExport'] for x in parent['reports'])
    workers.extend(p for p in parent['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker'])
assert len(workers)==24 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in workers)
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=290,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=seconds,sourceAndBinariesUnchanged=True,reports=reports,actualAdoptedSourceReviews=6,comparedSamples=sum(c['comparedSamples'] for r in history['reports'] for c in r['reviews']),historyChecks=sum(r['checks'] for r in history['reports']),historyRefusals=sum(len(r['refusals']) for r in history['reports']),plannedRecipes=12,retainedLegacyRecipes=6,allObservedProcessesReaped=True,newRendering=False,nativeWindowsQualified=False,physicalAudio=False,qualityAdmission=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for root,prefix in [(scope,''),(actual[0],'history/'),(actual[1],'planning/')]:
        for f in sorted(root.rglob('*')):
            if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','reports']};index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),runs=[str(r) for r in actual],archiveBytes=archive.stat().st_size,entries=len(entries),seconds=seconds,historyChecks=receipt['historyChecks'],historyRefusals=receipt['historyRefusals'],comparedSamples=receipt['comparedSamples'])))
