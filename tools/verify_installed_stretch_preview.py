#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained owned installed-stretch media/state; no installed/native replay."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,struct,math,zipfile
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--receipt',type=Path,default=Path(__file__).resolve().parents[1]/'tests/results/M2/2026-10-09-installed-stretch-preview/qualification.json')
args=parser.parse_args();receipt=json.loads(args.receipt.read_bytes())
archive=args.receipt.parent/'capture.zip';sha=lambda b:hashlib.sha256(b).hexdigest()
assert archive.stat().st_size==receipt['archive']['bytes'] and archive.stat().st_size<4*1024*1024
assert sha(archive.read_bytes())==receipt['archive']['sha256']
z=zipfile.ZipFile(archive);infos=z.infolist();assert len(infos)==len({i.filename for i in infos})==88
assert sum(i.file_size for i in infos)<32*1024*1024
assert set(z.namelist())==set(receipt['archive']['entries']) and z.testzip() is None
for info in infos:
    p=PurePosixPath(info.filename);assert not p.is_absolute() and '..' not in p.parts and '\\' not in info.filename
    assert info.file_size<4*1024*1024
    entry=receipt['archive']['entries'][info.filename];b=z.read(info)
    assert len(b)==entry['bytes'] and sha(b)==entry['sha256']
class ArchivePath:
    def __init__(self,name):self.name=str(name)
    def __truediv__(self,part):return ArchivePath(self.name+'/'+str(part))
    def read_bytes(self):return z.read(self.name)
    def read_text(self):return self.read_bytes().decode('utf-8')
w=ArchivePath('workspace')
def load(n):return json.loads((w/n).read_bytes())
def wave(p):
 b=p.read_bytes();assert b[:4] in (b'RIFF',b'RF64') and b[8:12]==b'WAVE';o=12;fmt=None;data=None;d64=None
 while o+8<=len(b):
  tag,n=struct.unpack_from('<4sI',b,o);o+=8
  if tag==b'ds64':d64=struct.unpack_from('<QQQ',b,o)
  if tag==b'data' and n==0xffffffff:assert d64;n=d64[1]
  assert o+n<=len(b)
  chunk=b[o:o+n]
  if tag==b'fmt ':fmt=struct.unpack_from('<HHIIHH',chunk);assert fmt[0]==3 or (fmt[0]==65534 and struct.unpack_from('<I',chunk,24)[0]==3)
  if tag==b'data':assert data is None;data=chunk
  o+=n+(n&1)
 assert o==len(b) and fmt and data is not None
 assert fmt[5]==32 and fmt[4]==fmt[1]*4 and fmt[3]==fmt[2]*fmt[4]
 samples=struct.unpack('<'+'f'*(len(data)//4),data);assert all(math.isfinite(s) for s in samples)
 return {'channels':fmt[1],'rate':fmt[2],'frames':len(data)//fmt[4],'peak':max(abs(s) for s in samples),'fileSha256':sha(b),'sampleSha256':sha(data)},data
initial=load('initial-project.json');applied=load('applied-project.json');undone=load('undone-project.json');redone=load('redone-project.json');reopened=load('project-été-Κиїв/project.json')
assert undone==initial and redone==applied and reopened==applied
assert (w/'before-reopen-project.json').read_bytes()==(w/'project-été-Κиїв/project.json').read_bytes()
clip=applied['tracks'][0]['clips'][0];anchor=clip['stretch'];assert clip['lengthFrames']==12288
assert anchor['settings']=={'timeNumerator':3,'timeDenominator':2,'pitchMilliCents':700007,'formantPreserved':True}
assert anchor['sourceOrigin']=={'frame':17,'fraction':1,'denominator':2,'algorithm':'soundcurrent.src-positioned-best-v1'} and anchor['sourceFrames']==8192
assert initial['assets'][0]==applied['assets'][0] and len(applied['assets'])==2
raw,rawdata=wave(w/'project-été-Κиїв/media/raw-été.wav');assert raw['fileSha256']==anchor['sourceSha256']==initial['assets'][0]['sha256']
asset=applied['assets'][1];derived,dd=wave(w/'project-été-Κиїв'/asset['path']);marker=load(str(Path('project-été-Κиїв')/asset['path']).replace('audio.wav','complete.json'))
assert marker['complete'] is True and marker['audioSha256']==derived['fileSha256']==asset['sha256'] and marker['sampleSha256']==derived['sampleSha256']
assert derived['frames']==marker['writtenFrames']==marker['target']==12288 and derived['channels']==2 and derived['rate']==48000 and derived['peak']>1
assert marker['first']==17 and marker['firstFraction']==1 and marker['firstDenominator']==2 and marker['sourceSha256']==raw['fileSha256']
export,ed=wave(w/'project-été-Κиїв-mix.wav');again,ad=wave(w/'reopened.wav');assert dd==ed==ad and export==again
assert (w/'project-été-Κиїв-mix.wav').read_bytes()==(w/'reopened.wav').read_bytes()
for n in ['first-exit.txt','second-exit.txt']:assert (w/n).read_text().strip()=='0'
for n in ['user-process-status.txt','helper-status.txt']:
 s=(w/n).read_text();assert 'CapEff:\t0000000000000000' in s and 'NoNewPrivs:\t1' in s and 'Uid:\t1000\t1000\t1000\t1000' in s
assert (w/'helper-executable.txt').read_text().strip()=='/usr/bin/sc-stretch-render-worker'
assert '--memory-mib\n256' in (w/'helper-command.txt').read_text()
for n in ['graph-before.json','graph-after-first.json','graph-after.json']:
 nodes=[x['info']['props']['node.name'] for x in load(n) if x['type']=='PipeWire:Interface:Node'];assert sorted(nodes)==sorted(['sc-preview-source','sc-preview-sink','Dummy-Driver','Freewheel-Driver'])
assert (w/'cycle-data-before.sha256').read_bytes()==(w/'cycle-data-after.sha256').read_bytes()

assert receipt['raw']==raw and receipt['derived']==derived and receipt['export']==export
assert receipt['sourceCommit']=='95cfb7d92a01abf82fa7c7f0963aec0b1fcbf698'
assert receipt['packageSha256']=='b959eae7715d92155f617a6d0bd79743dfc658e5e12451aded7753fb1cc59534'
assert receipt['normalQuitExits']==[0,0] and receipt['normalUserHelperCapabilities']==0
for key in ['exactPcmEquality','repeatedExportByteEquality','undoRedoSemanticEquality','reopenedSavedByteEquality','packageManagerUpgradeRemoveReinstall','userMediaPreservedOnRemove']:assert receipt[key] is True
for key in ['physicalAudio','vmStarted','hostPackageInstalled','fullParity']:assert receipt[key] is False
assert (w/'prior-version.txt').read_text().strip()==receipt['priorVersion']
assert (w/'installed-version.txt').read_text().strip()==receipt['version']
installed=dict((line.split()[1],line.split()[0]) for line in (w/'installed-binaries.sha256').read_text().splitlines())
assert installed['/usr/bin/soundcurrent-daw']=='0efcd3ea6b808846b9cabb618afaa5a79b7dede8d81aeedbbaee468b8ad55a08'
assert installed['/usr/bin/sc-stretch-render-worker']=='65899cc5ed579de6c950702eba511e6008556e9de9710c0970e9147917f5450e'
for line in (w/'cycle-reinstalled-binaries.sha256').read_text().splitlines():h,n=line.split();assert installed[n]==h
for n in z.namelist():
    if n.endswith('-command.json'):assert json.loads(z.read(n))['exit_code']==0
z.close()
print('installed_stretch_evidence: manifest88/ZIP CRC, actual owned RIFF/RF64, PCM/headroom, raw anchor, Undo/Redo/reopen, lifecycle and retained package identity verified; no native replay')
