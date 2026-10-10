#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect retained local observations; never replay DSP, a VM or an installer."""
from pathlib import Path, PurePosixPath
import hashlib, json, math, re, zipfile
folder=Path(__file__).resolve().parent
sha=lambda data:hashlib.sha256(data).hexdigest()
manifest=json.loads((folder/'manifest.json').read_bytes());path=folder/'capture.zip'
assert manifest['format']=='sc-local-explicit-region-capture-v1'
assert path.stat().st_size==manifest['bytes']<2*1024*1024 and sha(path.read_bytes())==manifest['sha256']
for key in ('nativeAudio','installedReplayed','fullProcessingQuality','productBinaryUploaded'):assert manifest[key] is False
with zipfile.ZipFile(path) as z:
 infos=z.infolist();assert len(infos)==len({i.filename for i in infos})==len(manifest['entries'])==17
 assert set(z.namelist())==set(manifest['entries']) and sum(i.file_size for i in infos)<4*1024*1024
 entries={}
 for i in infos:
  p=PurePosixPath(i.filename);assert not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename
  assert i.file_size<1024*1024;data=z.read(i)
  assert len(data)==manifest['entries'][i.filename]['bytes'] and sha(data)==manifest['entries'][i.filename]['sha256'];entries[i.filename]=data
history=json.loads(entries['verifier-history.json']);assert history['firstInspectionPassed'] is False and history['productionChangedForCorrection'] is False and history['testBinaryChangedForCorrection'] is False
assert history['observedSteps']==list(range(1,13)) and history['ninjaDisplayedTotal']==13
s=json.loads(entries['summary.json']);assert s['format']=='sc-local-explicit-region-observations-v1'
assert s['nativePlatform']=='linux' and s['sourceWasUncommitted'] is True and s['baseCommit']=='449c361f35fd18150dfe736df672583129484851'
for key in ('nativeAudio','vmStarted','nativeWindowsReplayed','installedReplayed','productBinaryUploaded','fullProcessingQuality','currentPackageReceipt'):assert s[key] is False
assert [s[k] for k in ('buildExitCode','focusedTests','coreWorkflows','uiWorkflows','stateChecks','uiChecks','controllerChecks','artifactChecks','callbackAudit','packageReceiptRefusals')]==[0,20,15,5,88,118,48,1045,0,57]
inputs=json.loads(entries['source-inputs.json'])['inputs'];assert len(inputs)==s['inputCount']==668
assert all(re.fullmatch('[0-9a-f]{64}',v) for v in inputs.values())
assert inputs['experiments/stretch-region-ratio/probe.cpp']==sha(entries['experiment/probe.cpp'])
exe=json.loads(entries['executables.json']);assert len(exe)==7
assert all(v['bytes']>0 and re.fullmatch('[0-9a-f]{64}',v['sha256']) for v in exe.values())
core=entries['logs/region-core-tests.log'].decode();ui=entries['logs/region-ui-tests.log'].decode()
assert '100% tests passed, 0 tests failed out of 15' in core and '100% tests passed, 0 tests failed out of 5' in ui
assert 'stretch_state_checks=88' in core and 'schema=1.14' in core
assert 'derived_shared_live_export_checks=1045 callback_audit=0' in core
assert 'stretch_ui_checks=118' in ui and 'explicit_context_128_frame=true context_live_export_split_seek_crop=true callback_audit=0' in ui
assert 'stretch_controller_checks=48' in ui
worker=json.loads(next(l.split(': ',1)[1] for l in core.splitlines() if l.startswith('4: {') and 'actualOwnedCompletedJobs' in l))
assert worker==s['worker'] and worker['checks']==383 and worker['actualOwnedCompletedJobs']==53 and worker['explicitRegionCompletedJobs']==12
for key in ('regionCopyExact','sameRoundedTargetDistinctKeys','realContextRefusalBeforeMutation'):assert worker[key] is True
assert worker['nativeAudio'] is False
geometry=json.loads(next(l.split(': ',1)[1] for l in core.splitlines() if 'actualCppExecuted' in l and 'nominalVisibleDuration' in l))
assert geometry==s['geometry'] and geometry['checks']==975 and geometry['accepted']==951 and geometry['refused']==20
assert geometry['actualCppExecuted'] is True and geometry['nativeAudio'] is False
assert '57 native render receipt refusals' in entries['logs/region-package-final.log'].decode()
assert [int(n) for n in re.findall(r'^\[(\d+)/147\]',entries['logs/region-build.log'].decode(),re.M)]==list(range(1,148))
assert [int(n) for n in re.findall(r'^\[(\d+)/13\]',entries['logs/region-final-build.log'].decode(),re.M)]==list(range(1,13))
extra=json.loads(entries['extra-checks.json']);assert len(extra)==5 and all(e['exitCode']==0 for e in extra)
run=json.loads(entries['experiment/region-ratio-run.json']);assert run['pid']==2161724 and run['exitCode']==0
assert run['addressSpaceCeilingBytes']==512*1024*1024 and run['timeoutSeconds']==60 and 0<run['seconds']<60
assert run['sourceSha256']==sha(entries['experiment/probe.cpp'])
for key in ('exeSha256','rubberBandLibrarySha256'):assert re.fullmatch('[0-9a-f]{64}',run[key])
for key in ('nativeAudio','nativeWindowsReplayed','productionChanged'):assert run[key] is False
rows=[json.loads(l) for l in entries['experiment/region-ratio-results.jsonl'].splitlines()]
assert len(rows)==37 and rows[-1]=={'summary':True,'renders':36,'nativeAudio':False,'fullProcessingQuality':False}
rows=rows[:-1];assert all(math.isfinite(r['peak']) and r['peak']>=0 for r in rows)
assert {(r['frames'],r['numerator'],r['denominator'],r['pitchMilliCents'],r['nominalRatio']) for r in rows}=={(f,n,d,p,b) for f in [8193,8319,8320] for n,d,p in [(1,1,0),(3,2,0),(3,2,700007),(1,4,-2400000),(4,1,2400000),(13,6,0)] for b in [False,True]}
assert all(r['ceilTarget']==(r['frames']*r['numerator']+r['denominator']-1)//r['denominator'] and r['exactCeil']==(r['writtenFrames']==r['ceilTarget']) for r in rows)
failed=[r for r in rows if not r['exactCeil']];assert len(failed)==1
assert [failed[0][k] for k in ('frames','numerator','denominator','writtenFrames','ceilTarget','nominalRatio')]==[8193,1,4,2048,2049,True]
print('Retained explicit-region observations verified:20 local workflows;53 worker jobs;118 UI/88 state/975 geometry checks;36 ratio renders with one nominal-duration counterexample. No current package, native audio, Windows, installer or full quality replay.')
