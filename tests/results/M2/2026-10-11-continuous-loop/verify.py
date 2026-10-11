#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded retained-log verification, optionally against supplied source bytes.

No Git-history dependency, code/audio replay, signature or authenticity claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
SOURCE='e84eaaac83a18742754368b619d43213d41224f7'
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--source-dir',type=Path);args=parser.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes())
assert s['format']=='sc-continuous-loop-core-log-v1'
raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','build.txt','ctest.txt','lasttest.txt'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(v.file_size for v in z.infolist())<2*1024*1024 and all(v.file_size<1024*1024 for v in z.infolist())
 b={n:z.read(n) for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['build-inputs.json'])
assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==504
for name,digest in i['files'].items():
 assert not name.startswith('/') and '..' not in name.split('/')
 assert name in ('CMakeLists.txt','.github/workflows/ci.yml') or name.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 assert re.fullmatch('[0-9a-f]{64}',digest)
 if args.source_dir:assert sha((args.source_dir/name).read_bytes())==digest,name
targets={'sc-loop-recording-tests','sc-loop-playback-tests','sc-duplex-recording-tests','sc-punch-tests','sc-manual-recording-tests','sc-take-lane-tests','sc-recording-tests','sc-recording-fault-tests','sc-export-tests','sc-session-tests'}
assert set(i['targets'])==set(i['executables'])==targets
assert all(re.fullmatch('[0-9a-f]{64}',v)for v in i['executables'].values())
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'],i['buildMode'],i['buildType'])==(2,10,2,7,'incremental','Debug')
assert b'ninja: no work to do.' in b['build.txt']
assert b'100% tests passed, 0 tests failed out of 11' in b['ctest.txt'] and b'No tests were found' not in b['ctest.txt']
suites={'session-state','duplex-recording-owner','punch-boundaries','manual-recording-control','offline-export','offline-export-media-policy','continuous-loop-recording','loop-stream-playback-recording','retained-take-comp','recording-recovery','recording-fault-persistence'}
passed=re.findall(rb'Test\s+#\d+: ([a-z-]+)\s+\.+\s+Passed',b['ctest.txt'])
assert len(passed)==len(suites) and {n.decode() for n in passed}==suites
marker=rb'continuous_loop_checks=(\d+) callback_audit=0 actual_passes=13 raw_files_per_run=2 short_periods=1,2,7,53 delayed_inputs=7,29 partial_explicit=true comp_export=true save_reopen=true original_context_durable=true interrupted_prefix=true bounded_context_inputs=256'
for log in ('ctest.txt','lasttest.txt'):
 counts=re.findall(marker,b[log]);assert len(counts)==1 and int(counts[0])==s['loopChecks']==4629
assert (s['qualifiedLocalSuites'],s['callbackAudit'],s['actualPasses'],s['rawFilesPerRun'],s['loopPeriodsFrames'],s['delaysFrames'])==(11,0,13,2,[1,2,7,53],[7,29])
for k in ('coreSyntheticQualified','originalContextDurable','interruptedPrefixVerified','partialRetentionExplicit','compExportSaveReopen'):assert s[k] is True
for k in ('nativeLinuxLoopRecordingQualified','nativeWindowsLoopRecordingQualified','desktopLoopRecordingQualified','recoveredCopyLoopReconciled','contextProjectCopyQualified','installedPreviewQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[k] is False
print('Continuous loop packet checked: 11 Linux suites, 13 synthetic passes, original intent and callback audit0. '+('504 source-file bytes compared. ' if args.source_dir else 'Source-file bytes not compared. ')+'No code/audio replay, signature or native/desktop loop recording qualification.')
