#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate bounded retained Linux clock-core logs and optional selected source.
No execution replay, historical Git requirement, signature or native audio claim.
"""
import argparse, hashlib, io, json, re, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SOURCE = '6d45a86ad87c7d91fb7b56ea723a069a5289e316'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir', type=Path)
args = parser.parse_args()
sha = lambda b: hashlib.sha256(b).hexdigest()
s = json.loads((ROOT/'summary.json').read_bytes())
m = json.loads((ROOT/'members.json').read_bytes())
raw = (ROOT/'capture.zip').read_bytes()
assert len(raw) == s['captureBytes'] < 256*1024 and sha(raw) == s['captureSha256']
expected = {'build-inputs.json', 'release-build.log', 'release-ctest.log',
            'release-lasttest.log', 'sanitizer-build.log', 'sanitizer-ctest.log',
            'sanitizer-lasttest.log'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(expected)
    assert set(z.namelist()) == set(m) == expected
    assert sum(v.file_size for v in z.infolist()) < 1024*1024
    assert all(v.file_size < 256*1024 for v in z.infolist())
    members = {n: z.read(n) for n in expected}
for n, b in members.items():
    assert m[n] == {'bytes': len(b), 'sha256': sha(b)}
i = json.loads(members['build-inputs.json'])
assert i['sourceCommit'] == s['sourceCommit'] == SOURCE
assert i['sourceTree'] == s['sourceTree'] and re.fullmatch('[0-9a-f]{40}', i['sourceTree'])
assert len(i['files']) == s['sourceInputs'] == 535
for n, h in i['files'].items():
    assert not n.startswith('/') and '..' not in n.split('/')
    assert n in ('CMakeLists.txt', '.github/workflows/ci.yml') or n.startswith(('include/', 'src/', 'ui/', 'tools/', 'tests/', 'localization/'))
    assert re.fullmatch('[0-9a-f]{64}', h)
    if args.source_dir:
        assert sha((args.source_dir/n).read_bytes()) == h, n
assert (i['parallel'], i['nice'], i['ioClass'], i['ioPriority']) == (2, 10, 2, 7)
assert set(i['executables']['release']) == {'sc-clock-correlation-tests', 'sc-native-render-timing-tests', 'sc-wasapi-input-tests', 'sc-wasapi-packet-copy-tests'}
assert set(i['executables']['sanitizer']) == {'sc-clock-correlation-tests'}
assert all(re.fullmatch('[0-9a-f]{64}', h) for bank in i['executables'].values() for h in bank.values())
assert set(i['commands']['exitCodes']) == {'releaseBuild', 'releaseCTest', 'sanitizerBuild', 'sanitizerCTest'}
assert all(v == 0 for v in i['commands']['exitCodes'].values())
assert i['commands']['sanitizerEnvironment'] == {'ASAN_OPTIONS':'detect_leaks=1', 'UBSAN_OPTIONS':'halt_on_error=1'}
assert i['commands']['sanitizerFlags'] == ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
assert b'src/clock_correlation.cpp.o' in members['release-build.log']
assert b'tests/clock_correlation_tests.cpp.o' in members['release-build.log']
assert b'100% tests passed, 0 tests failed out of 4' in members['release-ctest.log']
assert b'100% tests passed, 0 tests failed out of 1' in members['sanitizer-ctest.log']
for kind in ('release', 'sanitizer'):
    for suffix in ('-ctest.log', '-lasttest.log'):
        b = members[kind+suffix]
        assert b'4728 clock correlation checks passed (768 independent Fraction vectors)' in b
        assert b'20000 rolling RT observations/projections: cppAllocations=0 cppFrees=0 libcInterposition=enabled cAllocations=0 cFrees=0 blockingLocks=0' in b
assert (s['localReleaseSuites'], s['localSanitizerSuites'], s['clockChecks'], s['independentFractionVectors'], s['rollingRealtimeReadings']) == (4, 1, 4728, 768, 20000)
assert s['localCoreQualified'] and s['libcInterposition']
for n in ('cppAllocations','cppFrees','interposedCAllocations','interposedCFrees','interposedBlockingLocks'):
    assert s[n] == 0
for n in ('nativeWindowsCoreQualified','nativeDuplexQualified','clockModelQualityQualified','physicalAudioQualified','installedPreviewQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded','executablesIncluded'):
    assert s[n] is False
print('Clock-core packet checked: four release suites, sanitizer suite, 4728 checks, 768 Fraction vectors, 20000 rolling RT observations. '
      + ('535 selected source inputs compared. ' if args.source_dir else 'Source inputs not compared. ')
      + 'No replay, signature, native duplex, Windows or hardware qualification.')
