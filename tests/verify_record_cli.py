#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned CLI fixture and independent exact RF64 reader; no physical audio."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
tool = ROOT / '.cache/build-core/sc-record-tool'
with tempfile.TemporaryDirectory(prefix='s4-cli-', dir=ROOT / '.cache') as tmp:
    project = Path(tmp) / 'Enregistrement – Δοκιμή'
    first = subprocess.run([tool, 'synthetic', project], capture_output=True, text=True, check=True)
    job = next((project / 'media').iterdir())
    inspected = subprocess.run([tool, 'inspect', job], capture_output=True, text=True, check=True)
    before = hashlib.sha256((project / 'project.json').read_bytes()).hexdigest()
    refused = subprocess.run([tool, 'synthetic', project], capture_output=True, text=True)
    assert refused.returncode == 1
    assert before == hashlib.sha256((project / 'project.json').read_bytes()).hexdigest()
    data = (job / 'take.wav').read_bytes()
    assert data[:12] == b'RF64\xff\xff\xff\xffWAVE'
    offset, frames, fmt, payload = 12, None, None, None
    while offset + 8 <= len(data):
        name = data[offset:offset + 4]
        size = struct.unpack_from('<I', data, offset + 4)[0]
        offset += 8
        if name == b'ds64':
            riff, audio, frames = struct.unpack_from('<QQQ', data, offset)
            assert riff == len(data) - 8 and audio == 480000 * 4 and frames == 480000
        elif name == b'fmt ':
            fmt = data[offset:offset + size]
        elif name == b'data':
            assert size == 0xffffffff and frames == 480000
            payload = data[offset:offset + frames * 4]
            break
        offset += size + (size % 2)
    assert fmt is not None and payload is not None and len(payload) == 480000 * 4
    tag, channels, rate, byte_rate, align, bits = struct.unpack_from('<HHIIHH', fmt)
    assert channels == 1 and rate == 48000 and bits == 32 and align == 4 and byte_rate == 192000
    assert tag == 3 or (tag == 65534 and fmt[24:40] == bytes.fromhex('0300000000001000800000aa00389b71'))
    for i, (value,) in enumerate(struct.iter_unpack('<f', payload)):
        expected = struct.unpack('<f', struct.pack('<f', ((i % 101) - 50) * .04))[0]
        assert value == expected
    print(json.dumps({
        'synthetic_cli': first.stdout.strip(), 'inspect_cli': inspected.stdout.strip(),
        'refuses_existing_project': True, 'unicode_path': project.name,
        'independent_python_rf64_reader': {'frames': frames, 'rate': rate, 'channels': channels,
                                          'bits': bits, 'raw_sample_comparison': 'all 480000 float values exact'},
        'physical_device': False,
    }, ensure_ascii=False))
