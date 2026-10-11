#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate a bounded log packet and optionally compare frozen source bytes.
No Git-history dependency, code/audio replay or signature/authentication claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
SOURCE='0e65fc33d8e89df60f2fd50360bcd61cdadc8af9'
ROOT=Path(__file__).resolve().parent
args=argparse.ArgumentParser();args.add_argument('--source-dir',type=Path);args=args.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes())
raw=(ROOT/'capture.zip').read_bytes();assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','configure.txt','build.txt','ctest.txt','lasttest.txt','manual-ctest.txt','manual-lasttest.txt','localization.txt'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(r.file_size for r in z.infolist())<1024*1024 and all(r.file_size<512*1024 for r in z.infolist())
 b={n:z.read(n)for n in expected}
for n,value in b.items():assert m[n]=={'bytes':len(value),'sha256':sha(value)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==498
for name,digest in i['files'].items():
 assert not name.startswith('/') and '..' not in name.split('/')
 assert name=='CMakeLists.txt' or name.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 assert re.fullmatch('[0-9a-f]{64}',digest)
 if args.source_dir:assert sha((args.source_dir/name).read_bytes())==digest,name
assert set(i['targets'])==set(i['executables']) and len(i['targets'])==17
assert all(re.fullmatch('[0-9a-f]{64}',v)for v in i['executables'].values())
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'],i['buildMode'])==(2,10,2,7,'incremental')
assert b'100% tests passed, 0 tests failed out of 18' in b['ctest.txt']
assert b'100% tests passed, 0 tests failed out of 1' in b['manual-ctest.txt'] and b'manual-desktop-control' in b['manual-ctest.txt']
assert b'No tests were found' not in b['ctest.txt']+b['manual-ctest.txt']
counts=re.findall(rb'loop_stream_checks=(\d+) callback_audit=0 actual_passes=5 delayed_lanes=2 raw_unchanged=true live_export=true save_reopen=true',b['lasttest.txt'])
assert len(counts)==1 and int(counts[0])==s['loopChecks']==32129
assert b['lasttest.txt'].count(b'desktop_loop_checks=16 saved_undo_reopen=true raw_unchanged=true prepared_loop_until_stop=true source_fallback=true native_audio=false')==1
assert (s['qualifiedLocalSuites'],s['callbackAudit'],s['actualPasses'],s['desktopLoopChecks'],s['projectSchema'])==(19,0,5,16,'1.17')
assert s['localCohorts']==[{'suites':18,'seconds':41.06,'ctest':'ctest.txt','lasttest':'lasttest.txt'},{'suites':1,'seconds':0.75,'ctest':'manual-ctest.txt','lasttest':'manual-lasttest.txt'}]
assert s['localization']=={'sourceKeys':893,'partialCatalogs':34,'draftTranslations':3135,'nativeReviewed':0,'fullyUIQualified':0}
assert b'"source_keys": 893' in b['localization.txt'] and b'"fully_UI_qualified": 0' in b['localization.txt']
for k in ('desktopSimulatedLoopQualified','loopPreparationUntilStopQualified','rawTakesUnchanged','saveReopen','undoRedo'):assert s[k] is True
for k in ('nativeLinuxLoopQualified','nativeWindowsLoopQualified','automaticLoopRecordingQualified','physicalLoopQualified','installedPreviewQualified','compCrossfadesQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[k] is False
print('Desktop loop packet checked: 19 Linux suites in 18+1 cohorts, 16 GUI checks, five synthetic recorded passes. '+('498 source-file bytes compared. ' if args.source_dir else 'Source-file bytes not compared. ')+'No audio replay, signature, native audio, automatic loop-recording or installed-preview qualification.')
