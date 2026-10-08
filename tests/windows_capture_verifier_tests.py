#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Relocated media/integrity replay only. Never invokes Windows/native audio."""
import hashlib,json,subprocess,sys,tempfile,zipfile,shutil,struct
from pathlib import Path,PurePosixPath
root=Path(__file__).resolve().parents[1]
receipt=json.loads((root/'tests/results/X007/2026-10-08-windows-capture-foundation.json').read_text())
archive=root/'tests/results/X007'/receipt['archive']['name']
def require(ok,message):
 if not ok:raise ValueError(message)
require(archive.stat().st_size==receipt['archive']['bytes'] and hashlib.sha256(archive.read_bytes()).hexdigest()==receipt['archive']['sha256'],'Archive identity mismatch')
with tempfile.TemporaryDirectory(prefix='sc-native-win-replay-') as temporary:
 base=Path(temporary)
 with zipfile.ZipFile(archive) as z:
  require(z.testzip() is None,'CRC mismatch')
  expected={f['path']:f for f in receipt['payloadManifest']}
  require(len(z.infolist())==len(expected) and set(z.namelist())==set(expected),'Archive membership mismatch')
  for n in z.namelist():
   require(not n.startswith('/') and '..' not in PurePosixPath(n).parts,'Unsafe payload path')
   b=z.read(n);m=expected[n]
   require(len(b)==m['bytes'] and hashlib.sha256(b).hexdigest()==m['sha256'],'Payload identity mismatch')
   p=base/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
 project=base/receipt['finalAcceptedProject']
 def replay(p):
  return subprocess.run([sys.executable,str(root/'tests/verify_windows_capture.py'),str(p)],capture_output=True,text=True)
 r=replay(project);require(r.returncode==0,'Original replay refused: '+r.stderr)
 report=json.loads(r.stdout)
 failed=base/receipt['preservedFinalSourceFaultProject']
 p=json.loads((failed/'probe.json').read_text())
 faults=list(failed.rglob('first-fault.json'));require(len(faults)==1,'First native fault missing')
 fault=json.loads(faults[0].read_text())['fault']
 require(p['frames']==fault['capturedFrames']==77280 and not p['workflowAccepted'] and not p['reopenedAndExported'],'Fault prefix was promoted')
 require(fault['rejected']['discontinuity'] and fault['rejected']['position']>fault['previous']['position']+fault['previous']['duration'],'Original SDK gap missing')
 refused=0
 for case in range(7):
  q=base/('mutation-'+str(case));shutil.copytree(project,q)
  model=json.loads((q/'project.json').read_text());probe=json.loads((q/'probe.json').read_text())
  raw=q/model['assets'][0]['path'];export=q/'exports/native.wav'
  if case==0:probe['nonzeroSdkChannel1Samples']+=480
  if case==1:probe['workflowAccepted']=False
  if case==2:probe['firstBridgeFault']=True
  if case==3:probe['cppAllocations']=1
  if case in (4,5):
   file=export if case==4 else raw;b=bytearray(file.read_bytes());b[-32:]=struct.pack('<8f',*[.03125]*8);file.write_bytes(b)
   h=hashlib.sha256(b).hexdigest()
   if case==4:probe['exportSha256']=h
   else:model['assets'][0]['sha256']=probe['assetSha256']=h
  if case==6:raw.write_bytes(raw.read_bytes()[:-4])
  (q/'project.json').write_text(json.dumps(model));(q/'probe.json').write_text(json.dumps(probe))
  require(replay(q).returncode!=0,'Altered native claim/media accepted');refused+=1
 print(json.dumps({'archivePayloads':len(expected),'archiveIntegrityVerified':True,'relocatedNonzeroNativeMediaReplay':True,'maximumFloat32SampleError':report['maximumFloat32SampleError'],'alteredClaimsOrMediaRefused':refused,'originalSdkFaultPrefixRetained':77280,'nativeAudioReplayed':False}))
