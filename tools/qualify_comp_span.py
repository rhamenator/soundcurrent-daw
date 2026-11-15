#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain exact-source actual uniform-span rendering and independent analysis."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);a=p.parse_args()
build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if n.startswith(('include/','src/','third_party/','experiments/comp-span-render/')) or n in ['CMakeLists.txt','.github/workflows/ci.yml','tests/comp_span_render_tests.cpp','tests/comp_span_render_tests.py','tests/processing_grid_tests.cpp','tests/processing_grid_oracle.py','tools/qualify_comp_span.py']]
sources={n:sha(repo/n) for n in files};inputs=[build/n for n in ['sc-comp-span-render-tests','sc-processing-grid-tests']]+sorted(build.glob('lib*.a'));binaries={str(n):sha(n) for n in inputs}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));before=set((build/'comp-span-evidence').iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-uniform-span-render|exact-processing-grid|processing-grid-fraction-oracle)$']
start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
elapsed=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(code,scope)
new=set((build/'comp-span-evidence').iterdir())-before;assert len(new)==1;run=new.pop();child=json.loads((run/'process.json').read_text())
analysis_command=['/usr/bin/python3',str(repo/'experiments/comp-span-render/analyze.py'),str(run/'pcm')]
with (scope/'analysis.stdout').open('w') as out,(scope/'analysis.stderr').open('w') as err:
    analyzer=subprocess.Popen(analysis_command,stdout=out,stderr=err);analysis_code=analyzer.wait()
assert analysis_code==0,(scope,analysis_code)
report=json.loads((run/'pcm/report.json').read_text());analysis=json.loads((run/'pcm/analysis.json').read_text());assert report['actualR3Renders']==36 and not report['qualityAdmission']
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=273,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=elapsed,childProcess=child,analysisCommand=analysis_command,analysisPid=analyzer.pid,analysisExitCode=analysis_code,sourceAndBinariesUnchanged=True,checks=report['checks'],actualR3Renders=36,maxRelativeFrequencyError=analysis['maxRelativeFrequencyError'],dominantPeaksOutsideFundamentalGuard=analysis['dominantPeaksOutsideFundamentalGuard'],nonuniformProducerImplemented=False,ownedWorkerCompletion=False,formantsQualified=False,phaseQualified=False,qualityAdmission=False,nativeWindowsQualified=False,modelAdoptionChanged=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for root,prefix in [(scope,''),(run,'actual-span/')]:
        for f in sorted(root.rglob('*')):
            if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','childProcess']};index['childPid']=child['pid'];index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=elapsed,checks=report['checks'],maxRelativeFrequencyError=analysis['maxRelativeFrequencyError'],offFundamentalPeaks=analysis['dominantPeaksOutsideFundamentalGuard'])))
