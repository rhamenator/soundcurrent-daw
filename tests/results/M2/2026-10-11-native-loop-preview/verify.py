#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native observation packet consistency; optionally compare source bytes.
No historical Git, executable/PCM replay or signature/authentication claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
SOURCE='2b571181ca2166ccfac481a8eb1d71e14639ff1c'
ROOT=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source-dir',type=Path);args=parser.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'build-inputs.json','configure.txt','build.txt'}|{k+'-'+n for k in ('loop','linear')for n in ('stdout.json','stderr.txt','invocation.json','owned.conf','owned-media.json')}
with zipfile.ZipFile(io.BytesIO(raw))as z:
 assert len(z.namelist())==len(set(z.namelist()))==len(expected) and set(z.namelist())==set(m)==expected
 assert sum(r.file_size for r in z.infolist())<1024*1024 and all(r.file_size<512*1024 for r in z.infolist())
 b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['build-inputs.json']);assert i['sourceCommit']==s['sourceCommit']==SOURCE and i['sourceTree']==s['sourceTree']
assert len(i['files'])==s['sourceInputs']==499
for n,h in i['files'].items():
 assert not n.startswith('/') and '..'not in n.split('/') and re.fullmatch('[0-9a-f]{64}',h)
 assert n=='CMakeLists.txt' or n.startswith(('include/','src/','ui/','tools/','tests/','localization/'))
 if args.source_dir:assert sha((args.source_dir/n).read_bytes())==h,n
assert i['targets']==['sc-pipewire-ui-fixture'] and set(i['executables'])==set(i['targets'])
assert (i['parallel'],i['nice'],i['ioClass'],i['ioPriority'])==(2,10,2,7)
for k in ('loop','linear'):
 inv=json.loads(b[k+'-invocation.json']);r=json.loads(b[k+'-stdout.json']);assert r==s['observations'][k]
 assert inv['exitCode']==0 and inv['daemonRetired'] and inv['privateDaemon'] and not inv['vmUsed'] and not inv['physicalOutputSelected']
 assert set(inv['beforeNodes'])==set(inv['afterNodes'])=={'Dummy-Driver','Freewheel-Driver'}
 assert inv['fixtureSha256']==i['executables']['sc-pipewire-ui-fixture'] and inv['configSha256']==sha(b[k+'-owned.conf'])
 assert r['frames']==480000 and r['live_offline_difference']<=1e-7 and r['applied_events']==2 and 0<r['first_applied_frame']<480000
 assert all(r[n]==0 for n in ('missing_frames','rt_allocations','rt_frees','rt_blocking_locks'))
 assert all(r[n]is True for n in ('raw_unchanged','project_unchanged','owned_nodes_only','gui_playback','gui_live_edit','gui_undo','explicit_output_selection','gui_meter','asynchronous_close'))
 if k=='loop':assert r['native_loop_playback'] and r['loop_passes']>=27 and r['loop_stop_position']>=480000 and (r['loop_start'],r['loop_end'])==(1003,18776)
 else:assert not r['native_loop_playback'] and r['loop_passes']==0
 media=json.loads(b[k+'-owned-media.json']);assert 'project.json'in media and len([n for n in media if n.endswith('.wav')])==2
 for n,v in media.items():assert not n.startswith('/') and '..'not in n.split('/') and v['bytes']>0 and re.fullmatch('[0-9a-f]{64}',v['sha256'])
assert s['nativeLinuxWorkflows']==2
for n in ('privateNativeLinuxLoopQualified','linearNativeRegressionQualified'):assert s[n]is True
for n in ('nativeWindowsLoopQualified','physicalAudioQualified','automaticLoopRecordingQualified','installedPreviewQualified','M2Complete','fullGoalComplete','productArtifactUploaded','pcmIncluded'):assert s[n]is False
print('Native loop observation packet checked: two private Linux GUI workflows, 480000 compared samples each, 27 loop wraps. '+('499 frozen source-file bytes compared. 'if args.source_dir else'Source-file bytes not compared. ')+'No executable/PCM replay, signature or physical/Windows qualification.')
