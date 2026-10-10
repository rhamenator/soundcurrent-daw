#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained Linux selection publication logs and frozen inputs, without code/audio replay."""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p / '.git').exists())
HEAD = 'bff863a288f2f90f3184da86ecfe28e28c353cd2'
sha = lambda b: hashlib.sha256(b).hexdigest()
s = json.loads((ROOT / 'summary.json').read_bytes())
raw = (ROOT / 'capture.zip').read_bytes()
assert s['format'] == 'sc-selection-publication-linux-evidence-v1' and s['sourceCommit'] == HEAD
assert len(raw) == s['captureBytes'] and sha(raw) == s['captureSha256']
manifest = json.loads((ROOT / 'capture-members.json').read_bytes())
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(manifest) == 6
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
assert b'stretch_controller_checks=1520' in b['final-controller-tests.txt']
assert b'100% tests passed, 0 tests failed out of 1' in b['final-controller-tests.txt']
assert b'stretch_ui_checks=276' in b['final-ui-tests.txt']
assert b'100% tests passed, 0 tests failed out of 1' in b['final-ui-tests.txt']
assert b'stretch_controller_checks=1519' in b['intermediate-tests-before-save-assertion.txt']
assert s['controllerAssertions'] == 1520 and s['uiAssertions'] == 276
assert s['largeCorpus'] == {'actualCompletedHelperJobs': 129, 'ownedRemnants': 20,
    'remnantBytes': 20 * 512 * 1024, 'additionalIncompleteSelection': 1, 'totalExaminedEntries': 150}
assert s['savedProjectAndRawUnchanged'] is True and s['oldManifestUnchanged'] is True
assert s['maximumCompileJobs'] == 2 and (s['nice'], s['ioniceClass'], s['ioniceLevel']) == (10, 2, 7)
assert all(s[n] is False for n in ['simultaneousThroughputQualified', 'nativeAudioActivated',
    'vmStarted', 'windowsNativePublicationQualified', 'installedPublicationQualified',
    'storageFaultsQualified', 'powerLossQualified', 'fullGoalComplete', 'productUploaded'])
print('Retained Linux selection publication: 2 final tests, 309 frozen inputs, original intermediate acceptance and bounded scope audited; no code/audio replay')
