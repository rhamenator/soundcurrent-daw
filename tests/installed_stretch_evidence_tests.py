#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retained installed-stretch evidence refusals; no application/audio replay."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile,zipfile
root=Path(__file__).resolve().parents[1]
original=root/'tests/results/M2/2026-10-09-installed-stretch-preview'
verifier=root/'tools/verify_installed_stretch_preview.py'
checks=0
with tempfile.TemporaryDirectory(prefix='sc-installed-stretch-proof-') as td:
    out=Path(td)
    for case in ['positive','bad_archive_hash','wrong_scope','undo_changed','export_changed','raw_changed']:
        receipt=json.loads((original/'qualification.json').read_bytes())
        with zipfile.ZipFile(original/'capture.zip') as z:entries={n:z.read(n) for n in z.namelist()}
        if case=='wrong_scope':receipt['physicalAudio']=True
        if case=='undo_changed':
            n='workspace/undone-project.json';state=json.loads(entries[n]);state['name']='Changed by verifier fixture';entries[n]=json.dumps(state).encode()
        if case in ('export_changed','raw_changed'):
            n='workspace/reopened.wav' if case=='export_changed' else 'workspace/project-été-Κиїв/media/raw-été.wav'
            b=bytearray(entries[n]);b[-4:]=b'\x00\x00\x00\x00';entries[n]=bytes(b)
        with zipfile.ZipFile(out/'capture.zip','w',zipfile.ZIP_DEFLATED) as z:
            for n,b in entries.items():z.writestr(n,b)
        b=(out/'capture.zip').read_bytes();receipt['archive']={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'entries':{n:{'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()} for n,b in entries.items()}}
        if case=='bad_archive_hash':receipt['archive']['sha256']='0'*64
        (out/'qualification.json').write_text(json.dumps(receipt))
        r=subprocess.run([sys.executable,str(verifier),'--receipt',str(out/'qualification.json')],capture_output=True,text=True,timeout=15)
        assert (r.returncode==0)==(case=='positive'),(case,r.stdout,r.stderr)
        checks+=1
print('installed_stretch_evidence_checks='+str(checks)+' positive=true integrity_scope_state_media_refusals=true native_replay=false')
