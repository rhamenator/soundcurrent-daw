#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse retained scope/content inflation without executing captured scripts."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,tempfile,zipfile
root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
for case in ['scope','member-bytes','path','pcm-summary','undo','marker','windows-scope']:
 with tempfile.TemporaryDirectory(prefix='sc-retained-guard-') as directory:
  target=Path(directory)
  for name in ['verify.py','capture.zip','manifest.json','windows-attempt.json','windows-attempt.zip']:shutil.copyfile(root/name,target/name)
  manifest=json.loads((target/'manifest.json').read_text())
  expected={'scope':'Conservative scope','member-bytes':'Exact entry bytes','path':'Safe member path','pcm-summary':'Independent retained media summaries','undo':'Saved Undo/Redo project states','marker':'Exact persisted marker controls','windows-scope':'Windows unqualified scope'}[case]
  if case=='windows-scope':
   d=json.loads((target/'windows-attempt.json').read_text());d['nativeDesktopStartupQualified']=True;(target/'windows-attempt.json').write_text(json.dumps(d)+'\n')
  elif case=='scope':manifest['fullQualityQualified']=True
  else:
   with zipfile.ZipFile(target/'capture.zip') as z:files={n:z.read(n) for n in z.namelist()}
   if case=='member-bytes':files['first/workspace/helper-command.txt']+=b'changed'
   if case=='path':files['../unowned']=b'No execution'
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
print('installed_protected_retained_refusals: 7 exact negative cases; no captured script/audio/install execution')
