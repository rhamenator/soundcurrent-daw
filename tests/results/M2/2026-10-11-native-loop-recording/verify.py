#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check retained native loop recording logs; optionally compare source bytes.
No historical Git dependency, audio/UI replay, signature or authentication claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
SOURCE='9457f8747afb80a70ac4a096efacba4babef8f5c'
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-dir',type=Path);args=parser.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
keys=('loop','until-stop','normal')
expected={'build-inputs.json','build.txt','ctest.txt','last-test.txt'}|{k+'-'+n for k in keys for n in ('stdout.json','stderr.txt','invocation.json','owned.conf','project.json','contexts.json','owned-media.json')}
with zipfile.ZipFile(io.BytesIO(raw))as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(r.file_size for r in z.infolist())<2*1024*1024 and all(r.file_size<512*1024 for r in z.infolist())
 b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==504
for n,h in i['files'].items():
 assert not n.startswith('/') and '..'not in n.split('/') and re.fullmatch('[0-9a-f]{64}',h)
 assert n=='CMakeLists.txt' or n.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 if args.source_dir:assert sha((args.source_dir/n).read_bytes())==h,n
assert i['targets']==['sc-pipewire-duplex-ui-fixture','sc-ui-tests','sc-loop-recording-tests','sc-take-lane-tests']
assert set(i['executables'])==set(i['targets']) and all(re.fullmatch('[0-9a-f]{64}',h)for h in i['executables'].values())
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'])==(2,10,2,7)
assert b'100% tests passed, 0 tests failed out of 3'in b['ctest.txt'] and b'ninja: no work to do.'in b['build.txt']
assert b'desktop_loop_recording_checks=36'in b['last-test.txt'] and b'continuous_loop_checks=4629'in b['last-test.txt']
for k in keys:
 inv=json.loads(b[k+'-invocation.json']);r=json.loads(b[k+'-stdout.json']);assert r==s['observations'][k]
 assert inv['mode']==k and inv['exitCode']==0 and inv['daemonRetired'] and inv['privateDaemon'] and not inv['vmUsed'] and not inv['physicalOutputSelected']
 assert set(inv['beforeNodes'])==set(inv['afterNodes'])=={'Dummy-Driver','Freewheel-Driver'}
 assert inv['fixtureSha256']==i['executables']['sc-pipewire-duplex-ui-fixture'] and inv['configSha256']==sha(b[k+'-owned.conf'])
 assert r['armed_tracks']==3 and r['output_max_difference']==0
 assert all(r[n]==0 for n in ('missing_track_frames','callback_allocations','callback_frees','callback_locks'))
 assert all(r[n]is True for n in ('production_gui_duplex','raw_samples_exact','group_undo_redo','save_reopen','owned_nodes_only'))
 if k=='loop':assert r['native_loop_recording'] and not r['until_stop'] and (r['complete_passes'],r['partial_frames'],r['frames_per_raw_take'])==(13,0,231049)
 elif k=='until-stop':assert r['native_loop_recording'] and r['until_stop'] and r['complete_passes']>=13 and 0<r['partial_frames']<17773 and r['frames_per_raw_take']==r['complete_passes']*17773+r['partial_frames']
 else:assert not r['native_loop_recording'] and not r['until_stop'] and (r['complete_passes'],r['partial_frames'],r['frames_per_raw_take'])==(0,0,240000)
 media=json.loads(b[k+'-owned-media.json']);assert 'project.json'in media and len([n for n in media if n.endswith('.wav')])==5
 for n,v in media.items():assert not n.startswith('/') and '..'not in n.split('/') and v['bytes']>0 and re.fullmatch('[0-9a-f]{64}',v['sha256'])
 p=json.loads(b[k+'-project.json']);assert sha(b[k+'-project.json'])==media['project.json']['sha256'] and len(p['tracks'])==len(p['assets'])==4
 contexts=json.loads(b[k+'-contexts.json']);assert len(contexts)==(0 if k=='normal'else 3)
 if k!='normal':
  context=None;origins=set()
  for job,c in contexts.items():
   ctx,j=c['context'],c['journal'];assert context is None or ctx==context;context=ctx
   assert ctx['projectId']==j['projectId']==p['projectId'] and ctx['loop']=={'begin':137,'end':17910} and ctx['streamBegin']==137
   assert (ctx['schemaMajor'],ctx['schemaMinor'],ctx['sampleRate'])==(1,0,48000) and ctx['passes']==(13 if k=='loop'else None)
   assert len(ctx['sources'])==3 and job=='capture-'+j['assetId'] and j['phase']=='finalized' and j['recoveredFrom']is None
   assert (j['committedFrames'],j['startFrame'],j['inputLatencyFrames'])==(r['frames_per_raw_take'],137,0)
   assert j['rejectedFrames']==0 and j['endReason']==(2 if k=='loop'else 1) and j['timingOrigin']['backend']==2
   origins.add(j['timingOrigin']['devicePosition'])
   binding=[q for q in ctx['sources']if q['assetId']==j['assetId']];assert len(binding)==1 and binding[0]['trackId']==j['trackId']
   assert all(q['inputLatencyFrames']==0 and q['captureBegin']==137 for q in ctx['sources'])
  assert len(origins)==1
  for n,t in enumerate(p['tracks'][1:]):
   assert not t['clips'] and len(t['takeLanes'])==r['complete_passes']+(r['partial_frames']!=0)
   for passno,lane in enumerate(t['takeLanes']):
    clip=lane['clips'];assert len(clip)==1;clip=clip[0]
    assert clip['assetId']==p['assets'][n+1]['id'] and clip['sourceFrame']==passno*17773 and clip['startFrame']==137
    assert clip['lengthFrames']==(17773 if passno<r['complete_passes']else r['partial_frames'])
    assert lane['groupId']==p['tracks'][1]['takeLanes'][passno]['groupId'] and lane['name'].startswith('Прохід ')
assert s['nativeLinuxWorkflows']==3 and s['qualifiedLocalSuites']==3
for n in ('privateNativeLinuxLoopRecordingQualified','linearNativeRegressionQualified'):assert s[n]is True
for n in ('nativeDelayedInputQualified','nativeWindowsDuplexQualified','physicalAudioQualified','installedPreviewQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded','pcmIncluded'):assert s[n]is False
assert (ROOT/'PIPEWIRE-NOTICE.txt').is_file()
print('Native loop recording packet checked: finite, until-Stop and timed Linux GUI workflows; three regression suites. '+('504 frozen source files compared. 'if args.source_dir else'Source files not compared. ')+'No audio/UI replay, signature or Windows/physical/installed qualification.')
