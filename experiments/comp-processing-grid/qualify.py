#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain exact-source local timing tests and bounded owned PCM experiments."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('probe',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);args=p.parse_args()
build=args.build.resolve();probe=args.probe.resolve();evidence=args.evidence.resolve();public=args.public.resolve()
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain'), 'Commit source before qualifying'
files=git('ls-files').splitlines()
sources=[name for name in files if name.startswith(('src/','include/','third_party/','experiments/comp-processing-grid/')) or name in
         ['CMakeLists.txt','.github/workflows/ci.yml','tests/processing_grid_tests.cpp','tests/processing_grid_oracle.py','tests/fractional_timing_oracle_tests.py','tests/positioned_resampling_tests.cpp','tests/comp_handle_plan_tests.cpp']]
before={name:sha(repo/name) for name in sources};head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
executables=[probe,build/'sc-processing-grid-tests',build/'sc-positioned-resampling-tests',build/'sc-comp-handle-plan-tests']
libraries=[build/('lib'+name+'.a') for name in ['sc-session','sc-positioned-resampling','sc-rubberband']]
binaries={str(path):sha(path) for path in executables+libraries}
processes=[]
def run(command,name):
    started=time.monotonic()
    with (scope/(name+'.stdout')).open('w') as out,(scope/(name+'.stderr')).open('w') as err:
        process=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=process.wait()
    processes.append(dict(command=command,pid=process.pid,exitCode=code,seconds=time.monotonic()-started))
    assert code==0,(name,code,scope)
pattern='^(exact-processing-grid|processing-grid-fraction-oracle|comp-handle-envelope-plans|positioned-resampling-kernel|fractional-timing-oracle|clip-playback-rate|playback-rate-timing-oracle)$'
run(['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R',pattern],'tests')
(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
run([str(probe),str(scope/'pcm')],'render')
run([sys.executable,str(repo/'experiments/comp-processing-grid/analyze.py'),str(scope/'pcm')],'spectral-analysis')
assert before=={name:sha(repo/name) for name in sources} and binaries=={str(path):sha(path) for path in executables+libraries} and git('rev-parse','HEAD')==head
report=json.loads((scope/'pcm/report.json').read_text());analysis=json.loads((scope/'pcm/spectral-analysis.json').read_text())
receipt=dict(checkpoint=267,sourceHead=head,sourceTree=tree,sourceInputs=before,binaryInputs=binaries,processes=processes,
             sourceAndBinariesUnchanged=True,actualRenders=report['renders'],nominalComparison=report['nominalComparison'],
             compensatedMaxRelativeSpectralError=analysis['compensatedMaxRelativeSpectralError'],nativeWindowsQualified=False,
             fullCompProducer=False,formantsQualified=False,fullProcessingQuality=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(scope.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(scope).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={info.filename:dict(bytes=info.file_size,sha256=hashlib.sha256(z.read(info.filename)).hexdigest()) for info in z.infolist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs']}
index.update(scope='Exact worker input grid and pitch/full-rerender feasibility; production admission and quality remain open',
             archive=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries))
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),archiveBytes=archive.stat().st_size,entries=len(entries),renders=report['renders'],
                     nominalChangedSamples=report['nominalComparison']['changedSamples'],maximumSpectralError=analysis['compensatedMaxRelativeSpectralError'])))
