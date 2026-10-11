#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit original native CI refusal evidence; execute no captured code/audio."""
from pathlib import Path
import hashlib
import io
import json
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p / '.git').exists())
HEAD = '0b68766bdf0259f9ec2bce812dd4d7eb406a5930'
BASE = '7216066b45255e9ed6ae6890b4d9225da95647d8'
MERGE = 'e9afdf6bbc5ae44124ac429131215a9d07c63b57'
TREE = '578c9198cee9a97a0eb419ef1c653c768359bd83'
JOBS = {'114345233539', '114345233632', '114345233646', '114345233693'}
sha = lambda b: hashlib.sha256(b).hexdigest()

def git(*args):
    return subprocess.check_output(['git', *args], cwd=REPO)

s = json.loads((ROOT / 'summary.json').read_bytes())
assert s['format'] == 'sc-windows-publication-refusals-v1'
assert (s['sourceCommit'], s['testedBase'], s['testedMerge'], s['testedTree']) == (HEAD, BASE, MERGE, TREE)
assert s['runId'] == 38097129636 and s['nativeAssertions'] == 90
raw = (ROOT / 'capture.zip').read_bytes()
assert len(raw) == s['captureBytes'] and sha(raw) == s['captureSha256']
manifest = json.loads((ROOT / 'capture-members.json').read_bytes())
expected = {'qualification.json', 'artifact-receipt.json', 'LastTest.log', 'build-inputs.json'} | {'job-' + j + '.txt' for j in JOBS}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == 8
    assert set(z.namelist()) == expected == set(manifest)
    assert sum(i.file_size for i in z.infolist()) < 4 * 1024 * 1024
    assert all('/' not in i.filename and i.file_size < 1024 * 1024 for i in z.infolist())
    b = {n: z.read(n) for n in expected}
for n, data in b.items():
    assert manifest[n] == {'bytes': len(data), 'sha256': sha(data)}
    assert not data.startswith((b'MZ', b'\x7fELF'))
binding = json.loads(b['build-inputs.json'])
assert binding['sourceCommit'] == HEAD
assert binding['sourceTree'] == s['sourceTree'] == TREE
assert git('rev-parse', HEAD + '^{tree}').decode().strip() == TREE
# Synthetic CI merge objects need not exist in a checkout that fetched only
# the feature history. Audit its exact captured Git commit bytes in memory.
commit = (ROOT / 'tested-merge.commit').read_bytes()
assert len(commit) == s['testedMergeObjectBytes'] and len(commit) < 32768
assert sha(commit) == s['testedMergeObjectSha256']
assert hashlib.sha1(b'commit ' + str(len(commit)).encode() + b'\0' + commit,
                    usedforsecurity=False).hexdigest() == MERGE
header = commit.split(b'\n\n', 1)[0].splitlines()
trees = [line[5:].decode() for line in header if line.startswith(b'tree ')]
parents = [line[7:].decode() for line in header if line.startswith(b'parent ')]
assert trees == [TREE] and len(parents) == 2 and set(parents) == {HEAD, BASE}
paths = {p for p in git('ls-tree', '-r', '--name-only', HEAD, '--', 'include', 'src', 'ui', 'worker', 'tests', 'CMakeLists.txt').decode().splitlines() if p.endswith(('.cpp', '.hpp', '.c', '.h')) or p == 'CMakeLists.txt'}
assert paths == set(binding['files']) and len(paths) == s['sourceInputs'] == 310
for p in paths:
    blob = git('rev-parse', HEAD + ':' + p).decode().strip()
    assert binding['files'][p] == {'gitBlob': blob, 'sha256': sha(git('cat-file', 'blob', blob))}
q = json.loads(b['qualification.json'])
assert (q['source'], q['testedBase'], q['testedMerge'], q['testedTree']) == (HEAD, BASE, MERGE, TREE)
assert set(q['testedParents']) == {HEAD, BASE} and set(q['jobs']) == JOBS
assert q['nativePublicationAssertions'] == 90
assert q['parentAccessDeniedQualified'] and q['primitiveSharingQualified']
assert q['originalSourceLackedLaterRootValidation'] is True
for j in JOBS:
    log = b['job-' + j + '.txt']
    assert q['jobs'][j] == {'bytes': len(log), 'sha256': sha(log), 'checkout': MERGE, 'headBaseBound': True}
    assert ('Merge ' + HEAD + ' into ' + BASE).encode() in log
    lines = log.decode().splitlines()
    checkout = [lines[i + 1].split()[-1] for i, line in enumerate(lines[:-1]) if 'Run actions/checkout@' in line and 'log -1 --format=' in line]
    assert MERGE in checkout
receipt = json.loads(b['artifact-receipt.json'])
assert receipt['artifactId'] == 11686222691 and receipt['logMember'] == 'Testing/Temporary/LastTest.log'
assert receipt['logBytes'] == len(b['LastTest.log']) and receipt['logSha256'] == sha(b['LastTest.log'])
assert receipt['otherMembersExecuted'] is False and receipt['otherMembersExtracted'] is False
log = b['LastTest.log']
start = log.index(b'Testing: stretch-selection-publication')
end = log.index(b'"stretch-selection-publication" end time:', start)
native = log[start:end]
for line in [b'Native Windows security API handle warm-up: 85 -> 93', b'Native Windows parent creation denied with ERROR_ACCESS_DENIED; original DACL restored and retry verified', b'Native Windows no-overwrite primitive refused while source handle denied delete sharing; release/retry preserved payload', b'PASS: 90 owned selection publication checks', b'Test Passed.']:
    assert native.count(line) == 1, line
assert b'windows_access_denied_parent=true; windows_sharing_primitive=true' in native
assert b'native audio=false; Windows-process-interruption/full-disk/power-loss qualification=false' in native
for key in ['fullGoalComplete', 'currentUpdatedHeadQualified', 'installedPublicationQualified', 'windowsWriterInterruptionQualified', 'fullDiskPowerLossQualified', 'nativeAudioActivated', 'vmStarted', 'productUploaded']:
    assert s[key] is False
for key in ['currentUpdatedHeadQualified', 'installedPublicationQualified', 'windowsWriterInterruptionQualified', 'fullDiskPowerLossQualified', 'nativeAudio', 'vmUsed', 'productUploaded']:
    assert q[key] is False
print('Original Windows publication refusal CI: 90 native assertions, four checkout-bound job logs, 310 frozen inputs; no code/audio replay or current-head qualification')
