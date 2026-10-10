#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retained witness refusal cases; never run archived native programs or scripts."""
from pathlib import Path
import copy
import io
import json
import tempfile
import zipfile
from verify import ROOT, sha, verify

verify()
summary = json.loads((ROOT / 'summary.json').read_bytes())
with zipfile.ZipFile(ROOT / 'capture.zip') as archive:
    original = {n: archive.read(n) for n in archive.namelist()}
prefix = 'workspace/project-été-Κиїв/'
job = prefix + 'media/derived/' + summary['operation'] + '/'


def change_json(blobs, name, field, value):
    doc = json.loads(blobs[name]); doc[field] = value
    blobs[name] = json.dumps(doc).encode()


cases = [
    ('wrong actual helper exit', lambda b, s: change_json(b, 'workspace/helper-exit.json', 'exitCode', 0)),
    ('changed saved project', lambda b, s: change_json(b, prefix + 'project.json', 'playheadFrame', 1)),
    ('changed partial audio', lambda b, s: b.__setitem__(job + 'audio.partial', b[job + 'audio.partial'][:-1] + b'\xff')),
    ('changed selection operation', lambda b, s: change_json(b, prefix + 'media/stretch-selections/' + s['operation'] + '.json', 'operation', '00000000-0000-0000-0000-000000000000')),
    ('unrelated helper identity', lambda b, s: change_json(b, 'workspace/helper-signal.json', 'identity', dict(json.loads(b['workspace/helper-signal.json'])['identity'], pid=1))),
    ('SDK leaked into runtime', lambda b, s: b.__setitem__('workspace/developer-tools.txt', b'/usr/bin/cmake\n')),
    ('power-loss overclaim', lambda b, s: s.__setitem__('powerLossQualified', True)),
]
for name, mutate in cases:
    blobs, s = original.copy(), copy.deepcopy(summary)
    mutate(blobs, s)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        for n, data in blobs.items(): archive.writestr(n, data)
    packed = stream.getvalue(); s.update(captureSha256=sha(packed), captureBytes=len(packed))
    manifest = {n: {'bytes': len(data), 'sha256': sha(data)} for n, data in blobs.items()}
    with tempfile.TemporaryDirectory(prefix='sc-retained-gui-audit-') as folder:
        root = Path(folder)
        (root / 'capture.zip').write_bytes(packed)
        (root / 'summary.json').write_text(json.dumps(s))
        (root / 'capture-members.json').write_text(json.dumps(manifest))
        try: verify(root)
        except (AssertionError, KeyError): pass
        else: raise AssertionError('Admitted modified witness: ' + name)
print('Installed Linux GUI evidence: original and seven semantic refusal cases passed; no native replay')
