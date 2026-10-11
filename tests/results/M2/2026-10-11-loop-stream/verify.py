#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the bounded log packet; optionally compare a supplied frozen source tree.

No Git-history dependency, code/audio replay or signature/authentication claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
SOURCE='c6a4fa2e6a9ce143792e3440af90db0a135e4a5b'
ROOT=Path(__file__).resolve().parent
args=argparse.ArgumentParser();args.add_argument('--source-dir',type=Path);args=args.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes())
raw=(ROOT/'capture.zip').read_bytes();assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','configure.txt','build.txt','ctest.txt','lasttest.txt'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(r.file_size for r in z.infolist())<1024*1024 and all(r.file_size<512*1024 for r in z.infolist())
 b={n:z.read(n)for n in expected}
for n,value in b.items():assert m[n]=={'bytes':len(value),'sha256':sha(value)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==401
for name,digest in i['files'].items():
 assert not name.startswith('/') and '..' not in name.split('/')
 assert name=='CMakeLists.txt' or name.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 assert re.fullmatch('[0-9a-f]{64}',digest)
 if args.source_dir:assert sha((args.source_dir/name).read_bytes())==digest,name
assert set(i['targets'])==set(i['executables']) and len(i['targets'])==10
assert all(re.fullmatch('[0-9a-f]{64}',v)for v in i['executables'].values())
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'],i['buildMode'])==(2,10,2,7,'incremental')
assert b'100% tests passed, 0 tests failed out of 12' in b['ctest.txt'] and b'No tests were found' not in b['ctest.txt']
counts=re.findall(rb'loop_stream_checks=(\d+) callback_audit=0 actual_passes=5 delayed_lanes=2 raw_unchanged=true live_export=true save_reopen=true',b['lasttest.txt'])
assert len(counts)==1 and int(counts[0])==s['loopChecks']>31000
assert (s['qualifiedLocalSuites'],s['callbackAudit'],s['actualPasses'],s['delayedInputs'],s['delaysFrames'])==(12,0,5,2,[7,29])
for k in ('finiteSyntheticLoopQualified','rawTakesUnchanged','liveExportEqual','saveReopen','undoRedo'):assert s[k] is True
for k in ('desktopLoopControlsQualified','indefiniteLoopQualified','nativeWindowsLoopQualified','physicalLoopQualified','installedPreviewQualified','compCrossfadesQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[k] is False
print('Loop log packet checked: 12 Linux suites, five synthetic passes, callback audit0. '+('401 source-file bytes compared. ' if args.source_dir else 'Source-file bytes not compared. ')+'No code/audio replay, signature or native Windows qualification.')
