#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Semantic witness refusals with rebound archive/member hashes; no native replay."""
from pathlib import Path
import copy
import io
import json
import tempfile
import zipfile
from verify import ROOT, sha, audit

audit()
summary = json.loads((ROOT / 'summary.json').read_bytes())
with zipfile.ZipFile(ROOT / 'capture.zip') as z:
    original = {n: z.read(n) for n in z.namelist()}


def nested(blobs, suffix, mutate):
    name = summary['successfulCapture']
    with zipfile.ZipFile(io.BytesIO(blobs[name])) as z:
        entries = {n: z.read(n) for n in z.namelist()}
    target = next(n for n in entries if n.replace('\\', '/').endswith(suffix))
    entries[target] = mutate(entries[target])
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as z:
        for n, data in entries.items(): z.writestr(n, data)
    blobs[name] = stream.getvalue()


def wrong_review(data):
    rows = json.loads(data.decode('utf-8-sig'))
    for row in rows:
        if row['id'].endswith('.reviewRetainedStretch'): row['enabled'] = True
    return json.dumps(rows).encode()


def wrong_terminal(data):
    rows = json.loads(data.decode('utf-8-sig'))
    next(e['data'] for e in rows if e['name'] == 'helper-terminal')['exitCode'] = 0
    return json.dumps(rows).encode()


def move_event(data, name, anchor, after):
    rows = json.loads(data.decode('utf-8-sig'))
    row = next(e for e in rows if e['name'] == name)
    rows.remove(row)
    at = next(i for i, e in enumerate(rows) if e['name'] == anchor)
    rows.insert(at + int(after), row)
    return json.dumps(rows).encode()


def wrong_time(data):
    rows = json.loads(data.decode('utf-8-sig'))
    next(e for e in rows if e['name'] == 'helper-terminal')['utc'] = rows[0]['utc']
    return json.dumps(rows).encode()


def wrong_payload(data, reduced):
    receipt = json.loads(data.decode('utf-8-sig'))
    if reduced:
        receipt['payload'] = {n: receipt['payload'][n] for n in ['soundcurrent-daw.exe', 'sc-stretch-render-worker.exe']}
    else:
        receipt['payload']['Qt6Core.dll']['sha256'] = '0' * 64
    return json.dumps(receipt).encode()


def corrupt_input(blobs):
    doc = json.loads(blobs['processing-input-binding.json'])
    del doc['files']['ui/stretch_controller.cpp']
    blobs['processing-input-binding.json'] = json.dumps(doc).encode()


cases = [
    ('active Review enabled', lambda b, s: nested(b, '-scan-active.json', wrong_review)),
    ('incomplete Review enabled', lambda b, s: nested(b, '-scan-incomplete.json', wrong_review)),
    ('wrong actual helper exit', lambda b, s: nested(b, 'interruption-20261010223018382.json', wrong_terminal)),
    ('changed partial bytes', lambda b, s: nested(b, 'audio.partial', lambda d: d[:-1] + b'\xff')),
    ('changed opened project', lambda b, s: nested(b, 'project-restart-20261010223018382-opened-recovery.json', lambda d: d.replace(b'"playheadFrame": 0', b'"playheadFrame": 1'))),
    ('omitted processing input', lambda b, s: corrupt_input(b)),
    ('power-loss overclaim', lambda b, s: s.__setitem__('powerLossQualified', True)),
    ('parent death after Active', lambda b, s: nested(b, 'interruption-20261010223018382.json', lambda d: move_event(d, 'source-terminal', 'active-refused', True))),
    ('helper death before Active', lambda b, s: nested(b, 'interruption-20261010223018382.json', lambda d: move_event(d, 'helper-terminal', 'active-refused', False))),
    ('out-of-order event timestamp', lambda b, s: nested(b, 'interruption-20261010223018382.json', wrong_time)),
    ('reduced installed payload', lambda b, s: nested(b, 'installed-receipt.json', lambda d: wrong_payload(d, True))),
    ('changed installed DLL hash', lambda b, s: nested(b, 'installed-receipt.json', lambda d: wrong_payload(d, False))),
]
for label, change in cases:
    blobs, s = original.copy(), copy.deepcopy(summary)
    change(blobs, s)
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in blobs.items(): z.writestr(name, data)
    packed = output.getvalue(); s.update(captureBytes=len(packed), captureSha256=sha(packed))
    members = {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in blobs.items()}
    with tempfile.TemporaryDirectory(prefix='sc-win-installed-audit-') as folder:
        root = Path(folder)
        (root / 'capture.zip').write_bytes(packed)
        (root / 'capture-members.json').write_text(json.dumps(members))
        (root / 'summary.json').write_text(json.dumps(s))
        try: audit(root)
        except (AssertionError, KeyError, StopIteration): pass
        else: raise AssertionError('Admitted changed evidence: ' + label)
print('Installed Windows GUI evidence: original plus twelve semantic refusals passed; no native replay')
