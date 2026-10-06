#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent RIFF/RF64 parser and coefficient/sample oracle; owned files, no audio."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import signal
import struct
import subprocess
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=ROOT / '.cache/build-desktop')
args = parser.parse_args()
exporter = args.build / 'sc-export-tool'
recorder = args.build / 'sc-record-tool'
project_tool = args.build / 'sc-project-tool'

def run(*arguments, expected=0):
    p = subprocess.run([str(a) for a in arguments], capture_output=True, text=True, timeout=30)
    assert p.returncode == expected, (p.returncode, p.stdout, p.stderr)
    return p

def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]

def read_wave(path):
    data = path.read_bytes()
    assert data[8:12] == b'WAVE'
    assert data[:4] in (b'RIFF', b'RF64')
    rf64 = data[:4] == b'RF64'
    if not rf64:
        assert struct.unpack_from('<I', data, 4)[0] == len(data) - 8
    else:
        assert data[4:8] == b'\xff' * 4
    offset, payload, fmt, ds64 = 12, None, None, None
    while offset + 8 <= len(data):
        name = data[offset:offset+4]
        count = struct.unpack_from('<I', data, offset+4)[0]
        offset += 8
        if name == b'ds64':
            ds64 = struct.unpack_from('<QQQ', data, offset)
            assert ds64[0] == len(data)-8
        if name == b'fmt ':
            fmt = data[offset:offset+count]
        if name == b'data':
            if rf64:
                assert ds64 and count == 0xffffffff
                count = ds64[1]
            assert offset + count <= len(data)
            payload = data[offset:offset+count]
            break
        assert offset + count <= len(data)
        offset += count + count % 2
    assert fmt is not None and payload is not None
    tag, channels, rate, byte_rate, align, bits = struct.unpack_from('<HHIIHH', fmt)
    assert tag == 3 or (tag == 65534 and fmt[24:40] == bytes.fromhex('0300000000001000800000aa00389b71'))
    assert bits == 32 and align == 4 * channels and byte_rate == rate * align
    assert len(payload) % align == 0
    frames = len(payload) // align
    if rf64:
        assert ds64[2] == frames
    return dict(rf64=rf64, frames=frames, channels=channels, rate=rate,
                samples=[v[0] for v in struct.iter_unpack('<f', payload)], payload=payload)

with tempfile.TemporaryDirectory(prefix='s7-cli-', dir=ROOT / '.cache') as directory:
    base = Path(directory)
    project = base / 'Prise – Δοκιμή'
    recorded = run(recorder, 'synthetic', project)
    model_path = project / 'project.json'
    model = json.loads(model_path.read_text())
    track = model['tracks'][0]
    processor = track['processors'][0]
    for band in processor['bands']:
        band['gainDb'] = 0
    band = processor['bands'][1]
    band['gainDb'], band['frequencyHz'], band['q'] = 6, 1000, 1
    clip = track['clips'][0]
    clip.update(startFrame=237, sourceFrame=17, lengthFrames=10000)
    second = dict(clip, id=str(uuid.uuid4()), startFrame=2801, sourceFrame=1234, lengthFrames=1100)
    track['clips'].append(second)
    model['exportRange'] = dict(startFrame=137, endFrame=12003)
    model_path.write_text(json.dumps(model, ensure_ascii=False), encoding='utf-8')
    run(project_tool, 'inspect', project)  # Native state decoder reopens this fixture.
    hashes_before = {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in project.rglob('*') if p.is_file()}
    start, end = 137, 12003
    output = base / 'Éxport.wav'
    result = json.loads(run(exporter, 'render', project, output).stdout)
    wave = read_wave(output)
    assert not wave['rf64'] and wave['rate'] == 48000 and wave['channels'] == 1
    assert wave['frames'] == result['frames'] == end-start
    assert result['peak'] > 1 and result['over_full_scale_samples'] > 0
    # Independently evaluate one nonzero peaking stage with a direct-form-I history,
    # distinct from the native engine's transposed direct-form-II implementation.
    omega = 2*math.pi*1000/48000
    amplitude = 10**(6/40)
    alpha = math.sin(omega)/2
    a0 = 1+alpha/amplitude
    b0, b1, b2 = (1+alpha*amplitude)/a0, -2*math.cos(omega)/a0, (1-alpha*amplitude)/a0
    a1, a2 = b1, (1-alpha/amplitude)/a0
    x1 = x2 = y1 = y2 = 0.0
    oracle = []
    for frame in range(end):
        sample = 0.0
        for current in track['clips']:
            if current['startFrame'] <= frame < current['startFrame'] + current['lengthFrames']:
                source = current['sourceFrame'] + frame-current['startFrame']
                sample += f32(((source % 101)-50)*.04)
        x = f32(sample)
        y = b0*x+b1*x1+b2*x2-a1*y1-a2*y2
        x2, x1, y2, y1 = x1, x, y1, y
        if frame >= start:
            oracle.append(f32(y))
    difference = max(abs(a-b) for a, b in zip(wave['samples'], oracle, strict=True))
    assert difference <= 1e-7, difference
    assert hashlib.sha256(wave['payload']).hexdigest() == result['sample_sha256']
    assert hashlib.sha256(output.read_bytes()).hexdigest() == result['file_sha256']
    previous = output.read_bytes()
    run(exporter, 'render', project, output, expected=1)
    assert output.read_bytes() == previous
    fingerprint = run(exporter, 'fingerprint', output).stdout.strip()
    assert fingerprint == result['file_sha256']
    replacement = json.loads(run(exporter, 'render', project, output, '--rf64', '--replace-sha256', fingerprint).stdout)
    replaced_wave = read_wave(output)
    assert replacement['replaced'] and replacement['rf64'] and replaced_wave['rf64']
    assert replaced_wave['samples'] == wave['samples']
    # Stop a genuine long, admitted RF64 render after it has started writing.
    canceled_path = base / 'Cancel.wav'
    child = subprocess.Popen([str(exporter), 'render', str(project), str(canceled_path),
                              '--start', '0', '--end', '3000000000'],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic()+10
    writing = False
    try:
        while child.poll() is None and time.monotonic() < deadline:
            partials = list(base.glob('soundcurrent-export-*.partial'))
            if any(p.stat().st_size >= 1024*1024 for p in partials):
                writing = True
                child.send_signal(signal.SIGTERM)
                break
            time.sleep(.005)
        assert writing, 'Long export did not enter the write phase'
        stdout, stderr = child.communicate(timeout=10)
        assert child.returncode == 3, (child.returncode, stdout, stderr)
        assert not canceled_path.exists() and not list(base.glob('*.partial'))
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=10)
    assert hashes_before == {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in project.rglob('*') if p.is_file()}
    print(json.dumps(dict(record_cli=recorded.stdout.strip(), export_cli=result,
                         independent_reader='RIFF/RF64 chunks, float GUID, extents, every sample and digests',
                         independent_dsp='direct-form-I peaking oracle', maximum_sample_difference=difference,
                         confirmed_replacement='RF64 re-encoding preserves every sample',
                         signal_cancellation='SIGTERM during active large RF64 write; status 3, no output or temporary',
                         project_and_media_unchanged=True, physical_audio=False), ensure_ascii=False))
