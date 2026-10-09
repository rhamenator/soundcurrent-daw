#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
entry=json.loads((root/'third_party/manifest.json').read_text())['rubberband']
assert entry['version']=='4.0.0' and entry['license']=='GPL-2.0-or-later' and not entry['patches']
assert len(entry['files'])==63
for name,record in entry['files'].items():
    data=(root/'third_party/rubberband'/name).read_bytes()
    assert len(data)==record['bytes'] and hashlib.sha256(data).hexdigest()==record['sha256'],name
    if name!='COPYING':assert b'either version 2' in data and b'any later version' in data,name
assert not (root/'third_party/rubberband/otherbuilds').exists()
print('Rubber Band4.0.0:62 unchanged compiled inputs plus COPYING; exact hashes and GPL-or-later notices verified')
