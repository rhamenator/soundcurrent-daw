#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit frozen Linux publication failure logs; no test/audio replay."""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p / '.git').exists())
HEAD = 'a84e09bb90e2217b543f3774b42514cde9094f43'
sha = lambda b: hashlib.sha256(b).hexdigest()
s = json.loads((ROOT / 'summary.json').read_bytes())
raw = (ROOT / 'capture.zip').read_bytes()
manifest = json.loads((ROOT / 'capture-members.json').read_bytes())
assert s['format'] == 'sc-selection-publication-faults-linux-v1' and s['sourceCommit'] == HEAD
assert len(raw) == s['captureBytes'] and sha(raw) == s['captureSha256']
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(manifest) == 7
    assert z.testzip() is None and set(z.namelist()) == set(manifest)
    assert sum(i.file_size for i in z.infolist()) < 1024 * 1024
    assert all('/' not in i.filename and i.file_size < 512 * 1024 for i in z.infolist())
    b = {n: z.read(n) for n in z.namelist()}
for n, data in b.items():
    assert manifest[n] == {'bytes': len(data), 'sha256': sha(data)}
    assert not data.startswith((b'MZ', b'\x7fELF'))
binding = json.loads(b['build-inputs.json'])
assert binding['sourceCommit'] == HEAD and binding['sourceTree'] == s['sourceTree']
assert subprocess.check_output(['git', 'rev-parse', HEAD + '^{tree}'], cwd=REPO, text=True).strip() == s['sourceTree']
paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', HEAD, '--', 'include', 'src', 'ui', 'worker', 'tests', 'CMakeLists.txt'], cwd=REPO, text=True).splitlines()
paths = {n for n in paths if n.endswith(('.cpp', '.hpp', '.c', '.h')) or n == 'CMakeLists.txt'}
assert paths == set(binding['files']) and len(paths) == s['sourceInputs'] == 310
for n in paths:
    blob = subprocess.check_output(['git', 'rev-parse', HEAD + ':' + n], cwd=REPO, text=True).strip()
    data = subprocess.check_output(['git', 'cat-file', 'blob', blob], cwd=REPO)
    assert binding['files'][n] == {'gitBlob': blob, 'sha256': sha(data)}
assert len(binding['testBinarySha256']) == 64
assert b'No rule to make target' in b['build-initial.txt']
assert b'PASS: 316 owned selection publication checks' in b['tests-first.txt']
assert b['tests-first.txt'].count(b'Injected owned publication boundary 2 refused') == 2
assert s['earlierPrototypeQualified'] is False
log = b['tests-final.txt']
assert b'PASS: 351 owned selection publication checks' in log
assert b'100% tests passed, 0 tests failed out of 1' in log
assert b'Built target sc-stretch-publication-tests' in b['build-final.txt']
for boundary, final, error in [(2, 0, 28), (3, 0, 0), (4, 0, 5), (5, 0, 5), (6, 1, 5), (7, 1, 5)]:
    line = f'Injected owned publication boundary {boundary} refused; finalPresent={final} nativeError={error}'.encode()
    assert log.count(line) == 1
for boundary, partial, final in [(9, 1, 0), (10, 1, 0), (11, 1, 1), (12, 0, 1)]:
    line = f'Actual SIGKILL publication boundary {boundary} reaped; partialPresent={partial} finalPresent={final}'.encode()
    assert log.count(line) == 1
assert log.count(b'Actual owned child RLIMIT_FSIZE write refused with EFBIG; cleanup preserved files') == 1
assert log.count(b'Cancellation after actual file flush refused before publication; owned partial cleaned') == 1
assert s['parentAssertions'] == 351 and s['injectedErrorCases'] == 6 and s['actualSigkillBoundaries'] == 4
assert s['actualKernelWriteRefusal'] == 'EFBIG via child-only RLIMIT_FSIZE=128'
assert all(s[n] is True for n in ['shortWriteEintrRetry', 'postFlushCancellation', 'savedProjectRawAndOldManifestsUnchanged', 'nativeDescriptorAndResourceBaselineRestored', 'readOnlyRecoveryPreservedRemnants'])
assert s['maximumCompileJobs'] == 2 and (s['nice'], s['ioniceClass'], s['ioniceLevel']) == (10, 2, 7)
assert all(s[n] is False for n in ['nativeAudioActivated', 'vmStarted', 'actualFullFilesystemQualified', 'powerLossQualified', 'windowsFaultsQualified', 'installedPublicationQualified', 'processingQualityQualified', 'fullGoalComplete', 'productUploaded'])
print('Linux publication faults: 351 parent assertions, six injected errors, child EFBIG, four actual SIGKILL boundaries and 310 frozen inputs audited; no code/audio replay')
