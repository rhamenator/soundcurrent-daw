#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source-bound core assembly tests and actual separate edge/PCM observations."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('probe',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);args=p.parse_args()
build=args.build.resolve();probe=args.probe.resolve();evidence=args.evidence.resolve();public=args.public.resolve()
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain'),'Commit source before qualifying'
head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
sources=[name for name in git('ls-files').splitlines() if name.startswith(('src/','include/','third_party/','experiments/comp-edge-assembly/')) or name in
         ['CMakeLists.txt','.github/workflows/ci.yml','tests/comp_handle_pcm_tests.cpp','tests/processing_grid_tests.cpp','tests/processing_grid_oracle.py']]
before={name:sha(repo/name) for name in sources}
inputs=[probe,build/'sc-comp-handle-pcm-tests',build/'sc-processing-grid-tests']+[build/('lib'+name+'.a') for name in ['sc-comp-handle-pcm','sc-session','sc-positioned-resampling','sc-rubberband']]
binaries={str(path):sha(path) for path in inputs};processes=[]
def run(command,name):
    start=time.monotonic()
    with (scope/(name+'.stdout')).open('w') as out,(scope/(name+'.stderr')).open('w') as err:
        process=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=process.wait()
    processes.append(dict(command=command,pid=process.pid,exitCode=code,seconds=time.monotonic()-start))
    assert code==0,(name,code,scope)
run(['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-handle-pcm-assembly|exact-processing-grid|processing-grid-fraction-oracle)$'],'tests')
(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
run([str(probe),str(scope/'pcm')],'render');run([sys.executable,str(repo/'experiments/comp-edge-assembly/analyze.py'),str(scope/'pcm')],'analysis')
assert before=={name:sha(repo/name) for name in sources} and binaries=={str(path):sha(path) for path in inputs} and head==git('rev-parse','HEAD')
report=json.loads((scope/'pcm/report.json').read_text());analysis=json.loads((scope/'pcm/analysis.json').read_text())
receipt=dict(checkpoint=268,sourceHead=head,sourceTree=tree,sourceInputs=before,binaryInputs=binaries,processes=processes,sourceAndBinariesUnchanged=True,
             actualRenders=report['actualRenders'],cases=len(report['cases']),innerPcmBitPreserved=analysis['innerPcmBitPreserved'],
             maxJoinStep=analysis['maxJoinStep'],maxSlopeDeviation=analysis['maxSlopeDeviation'],qualityAdmission=False,
             nativeWindowsQualified=False,nativeEndpoints=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(scope.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(scope).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={info.filename:dict(bytes=info.file_size,sha256=hashlib.sha256(z.read(info.filename)).hexdigest()) for info in z.infolist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs']}
index.update(scope='Bounded PCM assembly and separate nonunity/pitched edge experiments; joins remain unqualified',archive=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries))
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),archiveBytes=archive.stat().st_size,entries=len(entries),renders=report['actualRenders'],cases=len(report['cases']),maxJoinStep=analysis['maxJoinStep'])))
