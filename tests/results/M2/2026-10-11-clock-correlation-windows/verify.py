#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check bounded retained Windows MSVC clock-core CI evidence, not native replay."""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir',type=Path)
args=parser.parse_args();sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<256*1024 and sha(raw)==s['captureSha256']
with zipfile.ZipFile(io.BytesIO(raw))as z:
    expected={'job.txt','status.json','reviewed-blobs.json'}
    assert len(z.namelist())==len(set(z.namelist()))==3 and set(z.namelist())==set(m)==expected
    assert sum(v.file_size for v in z.infolist())<1024*1024 and all(v.file_size<512*1024 for v in z.infolist())
    b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
st=json.loads(b['status.json']);files=json.loads(b['reviewed-blobs.json']);log=b['job.txt']
assert st['run']==38113199221 and st['job']==114392841598 and st['status']=='completed' and st['conclusion']=='success'
assert st['head']==s['head']=='13feb9f3c4b160897e241a4e0fe0287b0ad84f4a'
assert st['base']==s['base']=='eb863b9004db1c4be777da03e00b16afdea40176'
assert st['testedMerge']==s['testedMerge']=='da3f4adcab7ce0d7d532050951a55ab364a4357c'
assert st['tree']==s['tree']=='7e4f15e0123d5b4207033204620dc04a6d7df032'
assert ('Merge '+st['head']+' into '+st['base']).encode()in log
assert b'-DCMAKE_CXX_COMPILER=cl'in log and b'--target sc-clock-correlation-tests'in log
assert b'clock-correlation .......................   Passed    0.04 sec'in log
assert b'100% tests passed out of 53'in log and b'Total Test time (real) = 202.16 sec'in log
assert st['nativeCompiler']=='MSVC cl' and st['suites']==53 and st['totalSeconds']==202.16 and st['clockSuiteSeconds']==0.04
assert st['simulatedEndpoints'] and st['executableHashAvailable']is False
assert len(files)==s['reviewedBlobs']==17
for n,v in files.items():
    assert not n.startswith('/') and '..'not in n.split('/')
    assert re.fullmatch('[0-9a-f]{40}',v['gitBlob']) and re.fullmatch('[0-9a-f]{64}',v['sha256'])
    if args.source_dir:assert sha((args.source_dir/n).read_bytes())==v['sha256'],n
assert s['nativeWindowsCoreQualified']
for n in ('nativeDuplexQualified','physicalAudioQualified','installedPreviewQualified','clockModelQualityQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded','executableHashAvailable','executablesIncluded','countsObserved'):
    assert s[n]is False
print('Windows MSVC clock suite checked within53 successful synthetic suites; exact head/base/tested merge and17 reviewed blobs. '
      +('Selected bytes compared. 'if args.source_dir else'Bytes not compared. ')
      +'No native duplex/audio replay, executable-hash, per-check count, installed or full-goal claim.')
