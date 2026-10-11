#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit captured source/build/test provenance; no code or audio replay."""
from pathlib import Path
import hashlib,io,json,re,subprocess,zipfile
ROOT=Path(__file__).resolve().parent
REPO=next(p for p in ROOT.parents if (p/'.git').exists())
SOURCE='e092d7ea42d7ff949416d0174277f8320e94fa9e' # Replaced with full immutable source ID when packed.
sha=lambda b:hashlib.sha256(b).hexdigest()
git=lambda *args:subprocess.check_output(['git',*args],cwd=REPO)
s=json.loads((ROOT/'summary.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
m=json.loads((ROOT/'members.json').read_bytes());expected={'build-inputs.json','configure.txt','build.txt','ctest.txt','lasttest.txt','localization.txt'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(r.file_size for r in z.infolist())<1024*1024 and all('/'not in r.filename and r.file_size<512*1024 for r in z.infolist())
 b={name:z.read(name)for name in expected}
for name,value in b.items():
 assert m[name]=={'bytes':len(value),'sha256':sha(value)} and not value.startswith((b'MZ',b'\x7fELF',b'RIFF',b'RF64'))
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE
assert i['sourceTree']==s['sourceTree']==git('rev-parse',SOURCE+'^{tree}').decode().strip()
paths={p for p in git('ls-tree','-r','--name-only',SOURCE,'--','include','src','ui','tools','tests','localization','CMakeLists.txt').decode().splitlines() if p=='CMakeLists.txt' or p.endswith(('.cpp','.hpp','.h','.c')) or p.startswith('localization/')}
assert paths==set(i['files']) and len(paths)==s['sourceInputs']
for p in paths:assert i['files'][p]==sha(git('show',SOURCE+':'+p))
targets={'soundcurrent-daw','sc-manual-panel-tests','sc-timeline-ui-tests','sc-controller-tests','sc-localization-tests'}
assert set(i['targets'])==set(i['executables'])==targets and all(re.fullmatch('[0-9a-f]{64}',v) for v in i['executables'].values())
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'],i['buildMode'])==(2,10,2,7,'incremental')
assert b'Linking CXX executable soundcurrent-daw' in b['build.txt']
assert b'100% tests passed, 0 tests failed out of 8' in b['ctest.txt'] and b'No tests were found' not in b['ctest.txt']
counts=[int(n)for n in re.findall(rb'Retained desktop workflow: (\d+) assertions;',b['lasttest.txt'])]
assert counts==s['retainedWorkflowChecks'] and len(counts)==2 and all(n>=1700 for n in counts)
assert b['lasttest.txt'].count(b'callback audit zero')==2 and b'Manual desktop panel acceptance passed' in b['lasttest.txt']
assert json.loads(b['localization.txt'])=={'catalogs':34,'source_keys':877,'draft_translations':3135,'native_reviewed':0,'fully_UI_qualified':0}
assert (s['qualifiedLocalSuites'],s['callbackAudit'],s['capturedGroups'],s['tracksPerGroup'],s['declaredProjectionLanes'],s['virtualTakeRows'],s['sourceKeys'],s['catalogs'])==(8,0,2,2,131,132,877,34)
for k in ('rawTakesUnchanged','liveExportEqual','saveReopen','undoRedo'):assert s[k] is True
for k in ('nativeWindowsQualified','physicalAudioQualified','installedPreviewQualified','loopRecordingQualified','compCrossfadesQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[k] is False
print('Frozen desktop take/comp source and logs audited: eight Linux suites, two retained-workflow runs, callback audit0; no code/audio replay or Windows/loop/parity claim.')
