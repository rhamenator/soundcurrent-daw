#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain exact-source versioned comp recipe/recovery acceptance."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);args=p.parse_args()
build=args.build.resolve();evidence=args.evidence.resolve();public=args.public.resolve()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain'),'Commit implementation before qualifying'
head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','third_party/')) or n in
       ['CMakeLists.txt','.github/workflows/ci.yml','tests/comp_handle_pcm_tests.cpp','tests/comp_handle_blend_tests.cpp','tests/comp_processing_plan_tests.cpp','tools/qualify_comp_recipe.py','tests/comp_processing_recipe_tests.cpp']]
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-comp-handle-pcm-tests','sc-comp-handle-blend-tests','sc-comp-processing-plan-tests','sc-comp-processing-recipe-tests']]+sorted(build.glob('lib*.a'))
binaries={str(path):sha(path) for path in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-processing-recipe-recovery|comp-transition-processing-permissions|comp-handle-explicit-blend|comp-handle-pcm-assembly)$']
start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    process=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=process.wait()
elapsed=time.monotonic()-start
(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
assert code==0,(code,scope)
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(path):sha(path) for path in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=271,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=process.pid,exitCode=code,seconds=elapsed,
             sourceAndBinariesUnchanged=True,projectOutsidePcmBitExact=True,processingDomains=['physical-artifact','project-after-src'],actualRenderer=False,recipePersistence=True,sourceMediaVerified=False,trustedDigestRequired=True,
             modelAdoptionChanged=False,qualityAdmission=False,nativeWindowsQualified=False,fullDawParity=False)
assert 'comp_recipe_checks=618 physical=9 post_src=10 existing=3' in (scope/'LastTest.log').read_text()
receipt['recipeChecks']=618
receipt['recipeCases']=11
receipt['recipeDomains']={'physical':9,'projectAfterSrc':10,'existing':3}
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(scope.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(scope).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={i.filename:dict(bytes=i.file_size,sha256=hashlib.sha256(z.read(i.filename)).hexdigest()) for i in z.infolist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs']}
index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed)))
