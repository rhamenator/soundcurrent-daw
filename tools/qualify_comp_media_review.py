#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Freeze source/binaries, actual file-review outputs and owned-bank lineage."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args()
build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain'),'Commit source before qualification'
head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','third_party/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tests/comp_media_review_tests.cpp','tests/comp_media_review_tests.py','tests/comp_processing_recipe_tests.cpp','tests/comp_processing_plan_tests.cpp','tools/qualify_comp_media_review.py']]
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-comp-media-review-tests','sc-comp-processing-recipe-tests','sc-comp-processing-plan-tests']]+sorted(build.glob('lib*.a'))
binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
test_root=build/'comp-media-review-evidence';before=set(test_root.iterdir()) if test_root.exists() else set()
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-actual-media-review|comp-processing-recipe-recovery|comp-transition-processing-permissions)$']
start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    process=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=process.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
assert code==0,(code,scope)
new=set(test_root.iterdir())-before;assert len(new)==1;run=new.pop();child=json.loads((run/'process.json').read_text());report=json.loads((run/'results/report.json').read_text())
assert len(report['cases'])==36 and report['physicalFragments']>0 and report['postSrcFragments']>0 and report['existingFragmentsReviewed']==2
assert report['physicalSamplesCompared']>0 and report['projectSamplesCompared']>0
assert not any(report[k] for k in ['freshR3Renders','nativeEvidenceSemanticQualification','workerCompletionQualified','producerAlignmentQualified','qualityAdmission','modelAdoptionChanged'])
assert {(r['projectRate'],r['currentEncoding']) for r in report['cases']}=={(rate,encoding) for rate in [44100,48000,96000] for encoding in [0x6,0x7,0x3]}
assert head==git('rev-parse','HEAD') and sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs}
output_files={str(f.relative_to(run)):dict(bytes=f.stat().st_size,sha256=sha(f)) for f in run.rglob('*') if f.is_file() and 'bank' not in f.relative_to(run).parts}
receipt=dict(checkpoint=272,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=process.pid,exitCode=code,seconds=elapsed,sourceAndBinariesUnchanged=True,childProcess=child,outputFiles=output_files,reviewChecks=report['checks'],reviewCases=len(report['cases']),physicalFragments=report['physicalFragments'],postSrcFragments=report['postSrcFragments'],existingFragments=2,physicalSamplesCompared=report['physicalSamplesCompared'],projectSamplesCompared=report['projectSamplesCompared'],sourceMediaWaveFilesVerified=True,declaredCompanionBytesVerified=True,nativeEvidenceSemanticsQualified=False,workerCompletionQualified=False,producerAlignmentQualified=False,qualityAdmission=False,modelAdoptionChanged=False,nativeWindowsQualified=False,fullDawParity=False,freshR3Renders=0)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for f in sorted(scope.iterdir()):
        if f.is_file():z.write(f,f.name)
    for f in sorted(run.rglob('*')):
        if f.is_file() and 'bank' not in f.relative_to(run).parts:z.write(f,'actual-file-review/'+f.relative_to(run).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','outputFiles','childProcess']}
index['childPid']=child['pid'];index['ownedSourceArchiveSha256']=child['ownedSourceArchiveSha256'];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,checks=report['checks'])))
