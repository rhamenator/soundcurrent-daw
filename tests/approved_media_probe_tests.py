#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual native CLI argument/Unicode boundary, owned files only; no audio."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

probe = str(Path(sys.argv[1]).resolve())
checks = 0

def check(value):
    global checks
    checks += 1
    assert value, 'Approved-media probe boundary failed'

with tempfile.TemporaryDirectory(prefix='sc-media-été-Κиїв-') as folder:
    root = Path(folder)
    original = b'owned original media bytes\x00\xff'
    name = 'été-Κиїв.wav'
    (root / name).write_bytes(original)
    def run(arguments):
        return subprocess.run([probe, *arguments], capture_output=True, timeout=10)
    args = ['--root', str(root), '--relative', name, '--maximum-bytes', str(len(original))]
    result = run(args)
    check(result.returncode == 0 and not result.stderr)
    report = json.loads(result.stdout)
    check(report == {'protocol': 'sc-approved-media-check-v1', 'complete': True,
                     'relative': name, 'bytes': len(original),
                     'sha256': hashlib.sha256(original).hexdigest(), 'decodedAudio': False})
    for value in ('0', '-1', '18446744073709551616', '3x', '1'):
        bad = args.copy()
        bad[-1] = value
        failure = run(bad)
        check(failure.returncode != 0 and not failure.stdout)
        check(json.loads(failure.stderr)['complete'] is False)
    for value in ('../' + name, '/' + name, 'missing.wav', 'NUL.wav', 'nested/../' + name):
        bad = args.copy()
        bad[3] = value
        failure = run(bad)
        check(failure.returncode != 0 and not failure.stdout)
        check(json.loads(failure.stderr)['complete'] is False)
    check((root / name).read_bytes() == original)
print(f'PASS: {checks} actual approved-media CLI checks; no audio decode/conversion')
