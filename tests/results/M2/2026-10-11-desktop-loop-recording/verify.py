#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check retained desktop logs/pixels, optionally against supplied source bytes.

No Git-history dependency, audio/UI replay, signature or authenticity claim.
"""
import argparse,hashlib,io,json,re,struct,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
SOURCE='f792f74273c38f333726452bea58ac3d5da8ae62'
parser=argparse.ArgumentParser();parser.add_argument('--source-dir',type=Path);args=parser.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes())
assert s['format']=='sc-desktop-loop-recording-log-v1'
raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','build.txt','ctest.txt','lasttest.txt'}
with zipfile.ZipFile(io.BytesIO(raw))as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(v.file_size for v in z.infolist())<2*1024*1024 and all(v.file_size<1024*1024 for v in z.infolist())
 b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==504
for n,h in i['files'].items():
 assert not n.startswith('/') and '..'not in n.split('/')
 assert n in ('CMakeLists.txt','.github/workflows/ci.yml')or n.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 assert re.fullmatch('[0-9a-f]{64}',h)
 if args.source_dir:assert sha((args.source_dir/n).read_bytes())==h,n
targets={'soundcurrent-daw','sc-ui-tests','sc-recording-controller-tests','sc-manual-panel-tests','sc-controller-tests','sc-localization-tests','sc-loop-recording-tests','sc-loop-playback-tests','sc-take-lane-tests'}
assert set(i['targets'])==set(i['executables'])==targets
assert all(re.fullmatch('[0-9a-f]{64}',h)for h in i['executables'].values())
assert(i['parallel'],i['nice'],i['ioClass'],i['ioPriority'],i['buildMode'],i['buildType'])==(2,10,2,7,'incremental','Debug')
assert i['screenshotRequested']is True
assert b'Linking CXX executable sc-localization-tests'in b['build.txt']
assert b'100% tests passed, 0 tests failed out of 10'in b['ctest.txt']and b'No tests were found'not in b['ctest.txt']
suites={'desktop-localization','desktop-controller','desktop-recording-controller','desktop-loop-recording','desktop-loop-controls','desktop-ui','desktop-retained-takes','continuous-loop-recording','loop-stream-playback-recording','retained-take-comp'}
passed=re.findall(rb'Test\s+#\d+: ([a-z-]+)\s+\.+\s+Passed',b['ctest.txt'])
assert len(passed)==len(suites)and {n.decode()for n in passed}==suites
marker=rb'desktop_loop_recording_checks=(\d+) actual_passes=13 partial_explicit=true original_context_verified=true scalar_rebase=true raw_unchanged=true save_reopen=true native_audio=false'
for log in ('ctest.txt','lasttest.txt'):
 found=re.findall(marker,b[log]);assert len(found)==1 and int(found[0])==s['desktopLoopChecks']==37
assert(s['qualifiedLocalSuites'],s['optionalScreenshotChecks'],s['actualFinitePasses'],s['completePageSizes'],s['untilStopCompletePasses'],s['untilStopPartialFrames'])==(10,1,13,[5,8],6,37)
picture=s['snapshot'];assert picture['file']=='preview.png'
png=(ROOT/picture['file']).read_bytes();assert len(png)==picture['bytes']<1024*1024 and sha(png)==picture['sha256']
assert png[:8]==b'\x89PNG\r\n\x1a\n'and struct.unpack('>II',png[16:24])==(picture['width'],picture['height'])==(900,700)
for k in ('syntheticDesktopQualified','originalContextVerified','scalarRebase','rawUnchanged','saveReopen','undoRedo'):assert s[k]is True
for k in ('nativeLinuxLoopRecordingQualified','nativeWindowsLoopRecordingQualified','nativeWindowsDesktopLoopQualified','contextProjectCopyQualified','recoveredCopyLoopReconciled','installedPreviewQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[k]is False
print('Desktop loop packet checked: 10 Linux suites, 37 checks including one optional image check. '+('504 source-file bytes compared. 'if args.source_dir else'Source-file bytes not compared. ')+'No UI/audio replay, signature or native/Windows workflow qualification.')
