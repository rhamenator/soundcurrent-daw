#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact-source scope/GUI tests and immutable-input join blend observations."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('core',type=Path);p.add_argument('desktop',type=Path);p.add_argument('probe',type=Path);p.add_argument('input',type=Path);p.add_argument('evidence',type=Path);p.add_argument('public',type=Path);args=p.parse_args()
core=args.core.resolve();desktop=args.desktop.resolve();probe=args.probe.resolve();bank=args.input.resolve();evidence=args.evidence.resolve();public=args.public.resolve()
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
assert not git('status','--porcelain'),'Commit source before qualifying'
head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
old=repo/'tests/results/M2/2026-10-14-comp-edge-assembly';index=json.loads((old/'index.json').read_text());assert sha(old/'acceptance.zip')==index['archive']['sha256']
inputs={path.relative_to(bank).as_posix():sha(path) for path in bank.rglob('*') if path.is_file()}
assert inputs and all(value==index['archive']['entries']['pcm/'+name]['sha256'] for name,value in inputs.items())
sources=[name for name in git('ls-files').splitlines() if name.startswith(('src/','include/','ui/','third_party/','experiments/comp-explicit-blend/')) or name in
         ['CMakeLists.txt','.github/workflows/ci.yml','tests/comp_handle_pcm_tests.cpp','tests/comp_handle_blend_tests.cpp','tests/import_media_ui_tests.cpp']]
before={name:sha(repo/name) for name in sources}
binaries=[probe,core/'sc-comp-handle-pcm-tests',core/'sc-comp-handle-blend-tests',core/'libsc-comp-handle-pcm.a',core/'libsc-session.a']
binaries += [desktop/name for name in ['sc-import-media-ui-tests','sc-import-inspect-worker','sc-approved-wave-probe','sc-media-import-worker','sc-media-copy-lifecycle-probe','sc-import-lifecycle-probe']]
binaryHashes={str(path):sha(path) for path in binaries}
evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence));processes=[]
def run(command,name):
    start=time.monotonic()
    with (scope/(name+'.stdout')).open('w') as out,(scope/(name+'.stderr')).open('w') as err:
        process=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=process.wait()
    processes.append(dict(command=command,pid=process.pid,exitCode=code,seconds=time.monotonic()-start));assert code==0,(name,code,scope)
run(['ctest','--test-dir',str(core),'--output-on-failure','--no-tests=error','-R','^(comp-handle-explicit-blend|comp-handle-pcm-assembly)$'],'core-tests')
(scope/'core-LastTest.log').write_bytes((core/'Testing/Temporary/LastTest.log').read_bytes())
run(['ctest','--test-dir',str(desktop),'--output-on-failure','--no-tests=error','-R','^desktop-import-media-ui$'],'desktop-tests')
(scope/'desktop-LastTest.log').write_bytes((desktop/'Testing/Temporary/LastTest.log').read_bytes())
run([str(probe),str(bank),str(scope/'pcm')],'assemble')
run([sys.executable,str(repo/'experiments/comp-explicit-blend/analyze.py'),str(bank),str(scope/'pcm')],'analysis')
run([sys.executable,str(repo/'tools/localization.py'),'--check'],'catalogs')
assert before=={name:sha(repo/name) for name in sources} and binaryHashes=={str(path):sha(path) for path in binaries} and head==git('rev-parse','HEAD')
assert inputs=={name:sha(bank/name) for name in inputs}
analysis=json.loads((scope/'pcm/analysis.json').read_text());report=json.loads((scope/'pcm/report.json').read_text())
receipt=dict(checkpoint=269,sourceHead=head,sourceTree=tree,sourceInputs=before,binaryInputs=binaryHashes,retainedInputSourceHead=index['sourceHead'],retainedInputArchiveSha256=index['archive']['sha256'],
             retainedInputHashes=inputs,processes=processes,sourceBinariesAndRetainedInputsUnchanged=True,assemblies=report['assemblies'],additionalR3Renders=0,
             maxJoinStep=analysis['maxJoinStep'],maxSlopeDeviation=analysis['maxSlopeDeviation'],maximumCurveError=analysis['maximumCurveError'],
             modelVerifierChanged=False,qualityAdmission=False,nativeWindowsQualified=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n')
public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(scope.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(scope).as_posix())
    for name in sorted(inputs):z.write(bank/name,'retained-input/'+name)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    entries={info.filename:dict(bytes=info.file_size,sha256=hashlib.sha256(z.read(info.filename)).hexdigest()) for info in z.infolist()}
result={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','retainedInputHashes']}
result['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),archiveBytes=archive.stat().st_size,entries=len(entries),assemblies=report['assemblies'],maxSlopeDeviation=analysis['maxSlopeDeviation'])))
