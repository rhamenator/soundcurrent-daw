#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit frozen source/build/test provenance. Does not replay code or PCM."""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p/'.git').exists())
SOURCE = 'aabbae9bb8960b2a541d4bf635ca2f2888cecfc9'
sha = lambda b: hashlib.sha256(b).hexdigest()
git = lambda *args: subprocess.check_output(['git', *args], cwd=REPO)
s = json.loads((ROOT/'summary.json').read_bytes())
raw = (ROOT/'capture.zip').read_bytes()
assert len(raw) == s['captureBytes'] < 1024*1024 and sha(raw) == s['captureSha256']
members = json.loads((ROOT/'members.json').read_bytes())
expected = {'build-inputs.json', 'configure.txt', 'build.txt', 'ctest.txt', 'lasttest.txt'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(expected)
    assert set(z.namelist()) == set(members) == expected
    assert sum(entry.file_size for entry in z.infolist()) < 1024*1024
    assert all('/' not in entry.filename and entry.file_size < 512*1024 for entry in z.infolist())
    b = {name:z.read(name) for name in expected}
for name, value in b.items():
    assert members[name] == {'bytes':len(value), 'sha256':sha(value)}
    assert not value.startswith((b'MZ', b'\x7fELF', b'RIFF', b'RF64'))
i = json.loads(b['build-inputs.json'])
assert i['sourceCommit'] == s['sourceCommit'] == SOURCE
assert i['sourceTree'] == s['sourceTree'] == git('rev-parse', SOURCE+'^{tree}').decode().strip()
paths = {p for p in git('ls-tree', '-r', '--name-only', SOURCE, '--', 'include', 'src', 'ui', 'tools', 'tests', 'CMakeLists.txt').decode().splitlines() if p == 'CMakeLists.txt' or p.endswith(('.cpp', '.hpp', '.h', '.c'))}
assert paths == set(i['files']) and len(paths) == s['sourceInputs'] == 322
for path in paths:
    assert i['files'][path] == sha(git('show', SOURCE+':'+path))
targets = {'sc-take-lane-tests', 'sc-manual-recording-tests', 'sc-editing-tests', 'sc-history-resource-tests'}
assert set(i['targets']) == set(i['executables']) == targets
assert all(len(digest) == 64 for digest in i['executables'].values())
assert (i['parallel'], i['nice'], i['ioClass'], i['ioPriority']) == (2, 10, 2, 7)
for target in targets:
    assert ('Linking CXX executable '+target).encode() in b['build.txt']
assert b'100% tests passed, 0 tests failed out of 4' in b['ctest.txt']
assert b'No tests were found' not in b['ctest.txt']
assert b'retained_take_comp_checks=5257 callback_audit=0 raw_unchanged=true save_reopen=true grouped_offsets=true' in b['lasttest.txt']
assert b'"checks":65183' in b['lasttest.txt'] and b'"rt_violations":0' in b['lasttest.txt']
assert (s['takeCompChecks'], s['manualRecordingChecks'], s['callbackAudit'], s['qualifiedLocalSuites']) == (5257, 65183, 0, 4)
for key in ('rawTakesUnchanged', 'liveExportEqual', 'saveReopen', 'undoRedo', 'declaredEightTrackOffsets'):
    assert s[key] is True
for key in ('nativeWindowsQualified', 'desktopControlsQualified', 'loopRecordingQualified', 'compCrossfadesQualified', 'M2Complete', 'fullGoalComplete', 'productArtifactUploaded'):
    assert s[key] is False
print('Frozen take/comp source and logs: 5257 comp checks, 65183 manual checks, callback audit0; no code/audio replay or broad parity qualification.')
