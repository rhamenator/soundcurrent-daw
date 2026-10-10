#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect retained owned installed observations; no installation or audio replay."""
from pathlib import Path,PurePosixPath
import hashlib,json,math,struct,zipfile
root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
checks=0
def check(v,why):
 global checks
 checks+=1
 if not v:raise AssertionError(why)
def pcm(data):
 check(data[:4] in (b'RIFF',b'RF64') and data[8:12]==b'WAVE','WAV envelope')
 at=12;fmt=None;payload=None;size64=None
 while at+8<=len(data):
  name=data[at:at+4];size=struct.unpack_from('<I',data,at+4)[0];at+=8
  if name==b'ds64':size64=struct.unpack_from('<Q',data,at+8)[0]
  if name==b'data' and size==0xffffffff:check(size64 is not None,'RF64 data size');size=size64
  check(at+size<=len(data),'Bounded complete chunk')
  if name==b'fmt ':fmt=data[at:at+size]
  if name==b'data':check(payload is None,'Single PCM chunk');payload=data[at:at+size]
  at+=size+(size&1)
 check(fmt is not None and payload is not None and len(fmt)>=16,'PCM format/data')
 tag,channels,rate,byte_rate,align,bits=struct.unpack_from('<HHIIHH',fmt)
 if tag==0xfffe:check(len(fmt)>=40,'Extensible fmt');tag=struct.unpack_from('<H',fmt,24)[0]
 check(tag==3 and bits==32 and channels==2 and rate==48000 and align==8 and byte_rate==384000,'Stereo float format')
 check(len(payload)%align==0,'Complete frames')
 values=struct.unpack('<'+'f'*(len(payload)//4),payload)
 check(all(math.isfinite(x) for x in values),'Finite owned audio')
 return {'channels':channels,'rate':rate,'frames':len(payload)//align,'peak':max(map(abs,values)),'fileSha256':sha(data),'sampleSha256':sha(payload)},payload
manifest=json.loads((root/'manifest.json').read_text());archive=root/'capture.zip'
check(manifest['format']=='sc-installed-protected-preview-capture-v1','Manifest format')
check(archive.stat().st_size==manifest['bytes']<8*1024*1024 and sha(archive.read_bytes())==manifest['sha256'],'Archive admission/hash')
for key in ['physicalAudio','nativePlaybackStarted','fullQualityQualified','productBinaryUploaded']:check(manifest[key] is False,'Conservative scope')
with zipfile.ZipFile(archive) as z:
 check(set(z.namelist())==set(manifest['entries']) and len(z.namelist())==len(set(z.namelist())),'Exact unique member set')
 check(sum(x.file_size for x in z.infolist())<16*1024*1024,'Inflation bound')
 for x in z.infolist():
  path=PurePosixPath(x.filename);check(not path.is_absolute() and '..' not in path.parts and not x.is_dir(),'Safe member path')
  check(not x.filename.endswith(('.exe','.deb','.pyc','.conf')),'No product/bytecode/system config')
  data=z.read(x);row=manifest['entries'][x.filename];check(len(data)==row['bytes'] and sha(data)==row['sha256'],'Exact entry bytes')
 read=lambda n:json.loads(z.read(n))
 first=read('first/qualification.json');latest=read('upgraded/qualification.json')
 check(first['source']=='b0e61308e0531d45a2a5abab9d8a6cde5cc10999' and latest['source']=='083aefed3afd027406e1bb78e6f2d66b2f4dbfd8','Exact installed sources')
 check(latest['tree']=='fe3197c30e20c30493f03eb9ba9720a12f7de2e2','Reviewed final tree')
 check(latest['packageSha256']=='067abefdd87e554fd33712731aa051807e152908a97e6509c4e7ea0d3b10ed39','Paired package digest')
 package=json.loads((root/'package-receipt.json').read_text())
 prior=z.read('upgraded/workspace/prior-version.txt').decode().strip()
 installed=z.read('upgraded/workspace/installed-version.txt').decode().strip()
 check(prior==package['previous_version'] and installed==package['version']==latest['version'] and prior!=installed,'Installed package version receipts')
 check(latest['actualUpgradeFrom']==prior,'Actual upgrade base')
 check(package['source_head']==latest['source'] and package['source_tree']==latest['tree'] and package['previous_package_version_order_verified'] is True,'Paired source/version-order receipt')
 check(prior=='0.1.0~preview.20261010093051.b0e61308e053' and installed=='0.1.0~preview.20261010095352.083aefed3afd','Frozen actual package versions')
 upgrade=z.read('upgraded/workspace/install.log').decode()
 unpack=f'Unpacking soundcurrent-daw ({installed}) over ({prior}) ...'
 setup=f'Setting up soundcurrent-daw ({installed}) ...'
 check(upgrade.splitlines().count(unpack)==1 and upgrade.splitlines().count(setup)==1 and upgrade.index(unpack)<upgrade.index(setup),'Actual package upgrade log')
 check(all(z.read(prefix+'/workspace/installed-version.txt').decode().strip()==prior for prefix in ['first','reopened']),'Earlier installed package version')
 artifacts=package['artifacts']
 check(artifacts['soundcurrent-daw_'+installed+'_amd64.deb']['sha256']==latest['packageSha256'],'Exact installed package artifact receipt')
 source=json.loads((root/'source-archive-qualified.json').read_text())
 check(source['source']==latest['source'] and source['tree']==latest['tree'] and source['allArchiveContentsMatchGitExportPolicy'] is True and type(source['trackedBlobsCompared']) is int and source['trackedBlobsCompared']==2179,'Exact source archive observation')
 check(artifacts['soundcurrent-daw-'+installed+'-source.tar.gz']['sha256']==source['archiveSha256'],'Paired source archive digest')
 for observation in [first,latest]:
  for key in ['physicalAudio','nativePlaybackStarted','fullQualityQualified','hostPackageChanges']:check(observation[key] is False,'Installed observed scope')
  check(observation['normalQuitExit']==0,'Actual normal application exit')
 for prefix in ['first','reopened','upgraded']:
  check(read(prefix+'/container-exit.json')['exit']==0,'Actual overlay terminal result')
  check(z.read(prefix+'/workspace/first-exit.txt').strip()==b'0','Application exit')
  check(z.read(prefix+'/workspace/installed-binaries.sha256')==z.read('first/workspace/installed-binaries.sha256'),'All five payload identities')
  base=prefix+'/workspace/project-été-Κиїв/'
  raw,rpcm=pcm(z.read(base+'media/raw-été.wav'));derived,dpcm=pcm(z.read(base+'media/derived/a1e6f526-6231-475b-af59-5ab99af1f377/audio.wav'));export,epcm=pcm(z.read(prefix+'/workspace/project-été-Κиїв-mix.wav'))
  check(raw==first['raw'] and derived==first['derived'] and export==first['export'],'Independent retained media summaries')
  check(dpcm==epcm and raw['frames']==32768 and derived['frames']==49152 and raw['peak']==derived['peak']==1.5,'Complete PCM equality/headroom')
  check(z.read(base+'project.json')==z.read('first/workspace/redone-project.json'),'Save/reopen exact current project')
  def nodes(name):return {(x['id'],x.get('info',{}).get('props',{}).get('node.name')) for x in read(prefix+'/workspace/'+name) if x.get('type','').endswith(':Node')}
  check(nodes('graph-before.json')==nodes('graph-after-first.json'),'Private graph restored')
 initial=read('first/fixture-inspection-fixed.log');undone=read('first/workspace/undone-project.json');applied=read('first/workspace/applied-project.json');redone=read('first/workspace/redone-project.json')
 check(initial==undone and applied==redone and applied!=initial,'Saved Undo/Redo project states')
 stretch=applied['tracks'][0]['clips'][0]['stretch'];marker=stretch['warp']['markers'][0]
 check(stretch['settings']['timeNumerator']==3 and stretch['settings']['timeDenominator']==2 and marker=={'id':'84ad4062-8f82-4295-bff5-bf4172266777','source':[4096,0,1],'output':[6144,0,1]},'Exact persisted marker controls')
 helper=z.read('first/workspace/helper-status.txt').decode();user=z.read('first/workspace/user-process-status.txt').decode()
 for status in [helper,user]:check('Uid:\t1000\t1000\t1000\t1000' in status and 'CapEff:\t0000000000000000' in status and 'NoNewPrivs:\t1' in status,'Unprivileged owned process')
 check('/usr/bin/sc-stretch-render-worker' in z.read('first/workspace/helper-executable.txt').decode(),'Default installed sibling helper')
 check('--memory-mib\n256\n' in z.read('first/workspace/helper-command.txt').decode(),'Actual bounded helper command')
attempt=json.loads((root/'windows-attempt.json').read_text());wa=root/'windows-attempt.zip'
check(attempt['format']=='sc-windows-protected-preview-attempt-v1' and wa.stat().st_size==attempt['bytes']<4*1024*1024 and sha(wa.read_bytes())==attempt['sha256'],'Windows attempt envelope')
for key in ['nativeStretchBankQualified','nativeDesktopStartupQualified','installedPreviewQualified','physicalAudio','fullQualityQualified','productBinaryUploaded','causeResolved']:check(attempt[key] is False,'Windows unqualified scope')
with zipfile.ZipFile(wa) as z:
 check(len(z.namelist())==len(set(z.namelist())) and set(z.namelist())==set(attempt['entries']),'Windows unique members')
 check(sum(x.file_size for x in z.infolist())<4*1024*1024,'Windows inflation bound')
 for name,row in attempt['entries'].items():
  p=PurePosixPath(name);check(not p.is_absolute() and '..' not in p.parts and not name.endswith(('.exe','.dll','.ps1','.py')),'Windows observation-only member')
  data=z.read(name);check(len(data)==row['bytes'] and sha(data)==row['sha256'],'Windows exact entry')
 read=lambda n:json.loads(z.read(n).decode('utf-8-sig'))
 build=read('build-terminal.json');check(build['exitCode']==0 and build['sourceCommit']==attempt['sourceCommit'] and build['sourceTree']==attempt['sourceTree'] and build['nativeAudio'] is False,'Actual Windows build terminal')
 check(read('qualify-terminal.json')['failed'] is True and read('qualify-resumed-terminal.json')['failed'] is True,'Actual failed native qualification terminals')
 check('KeyError' in z.read('qualify-task.log').decode('utf-8-sig') and "'sha256'" in z.read('qualify-task.log').decode('utf-8-sig'),'Preserved obsolete harness field failure')
 check('Actual stretch failed' in z.read('qualify-resumed-task.log').decode('utf-8-sig'),'Preserved stretch failure')
 for name in ['main-task.log','main-v2-task.log','main-fusion-task.log']:check('No actual interactive main window' in z.read(name).decode('utf-8-sig'),'Preserved native window refusals')
 check(read('main-v2-observed-state.json')['windowHandle']==0,'Actual no-window observation')
 for name in ['inspection-worker-qualification.json','media-worker-qualification.json','copy-worker-qualification.json']:
  receipt=read(name);check(receipt['sourceCommit']==attempt['sourceCommit'] and receipt['pid']==receipt['reportedPid'] and receipt['pid']>0 and receipt['exitCode']==0,'Actual independent helper receipt')
 check(read('session-retired.json')['remainingApps']==0 and read('session-retired.json')['ownedTasksRunning']==0 and read('session-retired.json')['qualificationComplete'] is False,'Actual owned session retirement')
print(f'installed_protected_retained: {checks} checks; original owned WAV/project/process observations inspected; no DSP/native-audio replay or new installed qualification')
