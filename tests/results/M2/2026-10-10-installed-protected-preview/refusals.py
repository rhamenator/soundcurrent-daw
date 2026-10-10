#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse retained scope/content inflation without executing captured scripts."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,tempfile,zipfile
root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
for case in ['scope','member-bytes','path','pcm-summary','undo','marker','windows-scope','retained-old-version','fresh-install','upgrade-source']:
 with tempfile.TemporaryDirectory(prefix='sc-retained-guard-') as directory:
  target=Path(directory)
  for name in ['verify.py','capture.zip','manifest.json','windows-attempt.json','windows-attempt.zip','package-receipt.json','source-archive-qualified.json']:shutil.copyfile(root/name,target/name)
  manifest=json.loads((target/'manifest.json').read_text())
  expected={'scope':'Conservative scope','member-bytes':'Exact entry bytes','path':'Safe member path','pcm-summary':'Independent retained media summaries','undo':'Saved Undo/Redo project states','marker':'Exact persisted marker controls','windows-scope':'Windows unqualified scope','retained-old-version':'Installed package version receipts','fresh-install':'Actual package upgrade log','upgrade-source':'Actual upgrade base'}[case]
  if case=='windows-scope':
   d=json.loads((target/'windows-attempt.json').read_text());d['nativeDesktopStartupQualified']=True;(target/'windows-attempt.json').write_text(json.dumps(d)+'\n')
  elif case=='scope':manifest['fullQualityQualified']=True
  else:
   with zipfile.ZipFile(target/'capture.zip') as z:files={n:z.read(n) for n in z.namelist()}
   if case=='member-bytes':files['first/workspace/helper-command.txt']+=b'changed'
   if case=='path':files['../unowned']=b'No execution'
   if case=='retained-old-version':files['upgraded/workspace/installed-version.txt']=files['upgraded/workspace/prior-version.txt']
   if case=='fresh-install':
    before=files['upgraded/workspace/prior-version.txt'].decode().strip();files['upgraded/workspace/install.log']=files['upgraded/workspace/install.log'].replace((' over ('+before+')').encode(),b'')
   if case=='upgrade-source':
    d=json.loads(files['upgraded/qualification.json']);d['actualUpgradeFrom']='0.1.0~preview.20261009232400.f9a63532685c';files['upgraded/qualification.json']=(json.dumps(d)+'\n').encode()
   if case=='pcm-summary':
    d=json.loads(files['first/qualification.json']);d['derived']['frames']+=1;files['first/qualification.json']=(json.dumps(d)+'\n').encode()
   if case=='undo':
    d=json.loads(files['first/workspace/undone-project.json']);d['name']='Changed despite Undo';files['first/workspace/undone-project.json']=(json.dumps(d)+'\n').encode()
   if case=='marker':
    for name in ['first/workspace/applied-project.json','first/workspace/redone-project.json','first/workspace/project-été-Κиїв/project.json','reopened/workspace/project-été-Κиїв/project.json','upgraded/workspace/project-été-Κиїв/project.json']:
     d=json.loads(files[name]);d['tracks'][0]['clips'][0]['stretch']['warp']['markers'][0]['output'][0]+=1;files[name]=(json.dumps(d)+'\n').encode()
   with zipfile.ZipFile(target/'capture.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
    for name,data in files.items():z.writestr(name,data)
   manifest['bytes']=(target/'capture.zip').stat().st_size;manifest['sha256']=sha((target/'capture.zip').read_bytes())
   if case!='member-bytes':manifest['entries']={n:{'bytes':len(d),'sha256':sha(d)} for n,d in files.items()}
  (target/'manifest.json').write_text(json.dumps(manifest)+'\n')
  observed=subprocess.run([sys.executable,str(target/'verify.py')],capture_output=True,text=True,timeout=30)
  assert observed.returncode!=0 and expected in observed.stderr,(case,observed.returncode,observed.stderr)
  print(case+': refused for '+expected)
print('installed_protected_retained_refusals: 10 exact negative cases; no captured script/audio/install execution')
