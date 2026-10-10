#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained Linux paging logs and frozen inputs, without code/audio replay."""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p / '.git').exists())
HEAD = 'a8bf340ed233daaffc48523774000e776ba0c397'
sha = lambda b: hashlib.sha256(b).hexdigest()
s = json.loads((ROOT / 'summary.json').read_bytes())
raw = (ROOT / 'capture.zip').read_bytes()
assert s['format'] == 'sc-resumable-recovery-linux-evidence-v1' and s['sourceCommit'] == HEAD
assert len(raw) == s['captureBytes'] and sha(raw) == s['captureSha256']
manifest = json.loads((ROOT / 'capture-members.json').read_bytes())
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(manifest) == 10
    assert z.testzip() is None and set(z.namelist()) == set(manifest)
    assert sum(i.file_size for i in z.infolist()) < 4 * 1024 * 1024
    assert all('/' not in i.filename and i.file_size < 1024 * 1024 for i in z.infolist())
    b = {n: z.read(n) for n in z.namelist()}
for n, data in b.items():
    assert manifest[n] == {'bytes': len(data), 'sha256': sha(data)}
    assert not data.startswith((b'MZ', b'\x7fELF'))
binding = json.loads(b['build-inputs.json'])
assert binding['sourceCommit'] == HEAD and binding['sourceTree'] == s['sourceTree']
tree = subprocess.check_output(['git', 'rev-parse', HEAD + '^{tree}'], cwd=REPO, text=True).strip()
assert tree == s['sourceTree']
paths = subprocess.check_output(['git', 'ls-tree', '-r', '--name-only', HEAD, '--', 'include', 'src', 'ui', 'worker', 'tests', 'CMakeLists.txt'], cwd=REPO, text=True).splitlines()
paths = {n for n in paths if n.endswith(('.hpp', '.cpp', '.h', '.c')) or n == 'CMakeLists.txt'}
assert paths == set(binding['files']) and len(paths) == s['sourceInputs'] == 309
for n in paths:
    blob = subprocess.check_output(['git', 'rev-parse', HEAD + ':' + n], cwd=REPO, text=True).strip()
    data = subprocess.check_output(['git', 'cat-file', 'blob', blob], cwd=REPO)
    assert binding['files'][n] == {'sha256': sha(data), 'gitBlob': blob}
assert b'stretch_controller_checks=831' in b['final-stretch-tests.txt']
assert b'stretch_ui_checks=276' in b['final-stretch-tests.txt']
assert b'100% tests passed, 0 tests failed out of 2' in b['final-stretch-tests.txt']
assert b'PASS: 168 approved-root checks' in b['final-approved-tests.txt']
assert b'100% tests passed, 0 tests failed out of 1' in b['final-approved-tests.txt']
assert b'fatal error: media_io.hpp: No such file or directory' in b['initial-build-refused.txt']
assert b'No tests were found!!!' in b['incorrect-selector-refused.txt']
assert s['maximumCompileJobs'] == 2 and (s['nice'], s['ioniceClass'], s['ioniceLevel']) == (10, 2, 7)
assert all(s[n] is False for n in ['nativeAudioActivated', 'vmStarted', 'windowsNativePagingQualified', 'installedPagingQualified', 'manyCompletedJobsQualified', 'unboundedNewPublicationQualified', 'fullGoalComplete', 'productUploaded'])
print('Retained Linux paging: final 2+1 tests, 309 frozen source inputs and original failures audited; no code/audio replay')
