#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the complete pinned upstream source byte set before claiming its build."""
from pathlib import Path
import hashlib
import json

root=Path(__file__).resolve().parents[1]
entry=json.loads((root/'third_party/manifest.json').read_text())['libsamplerate_source']
base=root/'third_party/libsamplerate'
files={p.relative_to(base).as_posix():p for p in base.rglob('*') if p.is_file()}
assert set(files)==set(entry['files'])
for name,p in files.items():
    assert not p.is_symlink(),name
    data=p.read_bytes();expected=entry['files'][name]
    assert len(data)==expected['bytes'] and hashlib.sha256(data).hexdigest()==expected['sha256'],name
assert entry['version']=='0.2.2' and entry['license']=='BSD-2-Clause'
assert entry['sourceCommit']=='c96f5e3de9c4488f4e6c97f59f5245f22fda22f7'
assert entry['modifications']==[] and entry['build']['CONFIG_CHAN_NR']==256
assert entry['archive']['sha256']=='16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a'
print(json.dumps({'sourceFilesVerified':len(files),'bytes':sum(p.stat().st_size for p in files.values()),
                  'sourceVersion':entry['version'],'upstreamModified':False,
                  'localPGPVerification':False,'binaryRuntimeQualification':False}))
