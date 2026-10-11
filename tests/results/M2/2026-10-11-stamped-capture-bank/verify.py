#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate bounded retained capture-bank Linux logs and selected source bytes.
No replay, signature, native endpoint, Windows or installed workflow claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
SOURCE='2da7a3e2f664963f9230161031fa04290969527c'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir',type=Path)
args=parser.parse_args();sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<256*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','tsan-configure.log'}|{kind+suffix for kind in ('release','sanitizer','tsan') for suffix in ('-build.log','-ctest.log','-lasttest.log')}
with zipfile.ZipFile(io.BytesIO(raw))as z:
    assert len(z.namelist())==len(set(z.namelist()))==11 and set(z.namelist())==set(m)==expected
    assert sum(v.file_size for v in z.infolist())<1024*1024 and all(v.file_size<256*1024 for v in z.infolist())
    b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE
assert i['sourceTree']==s['sourceTree'] and re.fullmatch('[0-9a-f]{40}',i['sourceTree'])
assert len(i['files'])==s['sourceInputs']==538
for n,h in i['files'].items():
    assert not n.startswith('/') and '..'not in n.split('/')
    assert n in ('CMakeLists.txt','.github/workflows/ci.yml') or n.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
    assert re.fullmatch('[0-9a-f]{64}',h)
    if args.source_dir:assert sha((args.source_dir/n).read_bytes())==h,n
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'])==(2,10,2,7)
assert set(i['executables']['release'])=={'sc-capture-bank-tests','sc-clock-correlation-tests','sc-wasapi-packet-copy-tests'}
assert all(set(i['executables'][k])=={'sc-capture-bank-tests'}for k in ('sanitizer','tsan'))
assert all(re.fullmatch('[0-9a-f]{64}',h)for bank in i['executables'].values()for h in bank.values())
commands=i['commands'];assert commands['sourceCommit']==SOURCE
assert set(commands['exitCodes'])=={kind+suffix for kind in ('release','sanitizer','tsan')for suffix in ('-build','-ctest')}
assert all(v==0 for v in commands['exitCodes'].values())
assert commands['sanitizerFlags']==['-fsanitize=address,undefined','-fno-omit-frame-pointer']
assert commands['threadSanitizerFlags']==['-fsanitize=thread','-fno-omit-frame-pointer','-O1']
assert 'ASAN_OPTIONS=detect_leaks=1'in commands['commands']['sanitizer-ctest']
assert 'UBSAN_OPTIONS=halt_on_error=1'in commands['commands']['sanitizer-ctest']
assert 'TSAN_OPTIONS=halt_on_error=1'in commands['commands']['tsan-ctest']
for kind in ('release','sanitizer','tsan'):
    assert b'src/capture_bank.cpp.o'in b[kind+'-build.log'] and b'tests/capture_bank_tests.cpp.o'in b[kind+'-build.log']
    assert ('100% tests passed, 0 tests failed out of '+('3'if kind=='release'else'1')).encode()in b[kind+'-ctest.log']
    for suffix in ('-ctest.log','-lasttest.log'):
        assert b'3179 stamped capture bank checks passed'in b[kind+suffix]
        assert b'concurrentPackets=20000 concurrentFrames=319935 cppAllocations=0 cppFrees=0 libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0'in b[kind+suffix]
assert b'4728 clock correlation checks passed'in b['release-ctest.log']
assert (s['releaseSuites'],s['asanUbsanSuites'],s['threadSanitizerSuites'],s['bankChecks'],s['concurrentPackets'],s['concurrentFrames'])==(3,1,1,3179,20000,319935)
assert s['localCoreQualified'] and s['localThreadSanitizerQualified']
for n in ('cppAllocations','cppFrees','interposedCAllocations','interposedCFrees','interposedBlockingLocks'):assert s[n]==0
for n in ('nativeWindowsBankQualified','causalMonitoringQualified','driftResamplingQualified','nativeDuplexQualified','physicalAudioQualified','installedPreviewQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded','executablesIncluded'):assert s[n]is False
print('Capture-bank packet checked:3179 checks,20000 concurrent packets/319935 frames, three Release suites, ASan/UBSan and ThreadSanitizer. '
      +('538 selected source inputs compared. 'if args.source_dir else'Source inputs not compared. ')
      +'No replay, signature, native audio/Windows/installed or full-goal qualification.')
