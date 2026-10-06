#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent RIFF/RF64 and source-coordinate oracle; isolated files, no devices."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time
import uuid

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=root / '.cache/build-desktop')
args = parser.parse_args()
tool = args.build / 'sc-export-tool'

def run(*cmd, expected=0):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True, timeout=30)
    assert p.returncode == expected, (p.returncode, p.stdout, p.stderr)
    return p

def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]

def wave(path):
    b = path.read_bytes()
    assert b[8:12] == b'WAVE' and b[:4] in (b'RIFF', b'RF64')
    pos, size, channels = 12, None, None
    while pos+8 <= len(b):
        tag, n = struct.unpack_from('<4sI', b, pos)
        pos += 8
        if tag == b'ds64':
            riff, size, count = struct.unpack_from('<QQQ', b, pos)
            assert riff == len(b)-8
        if tag == b'fmt ':
            kind, channels, rate, _, stride, bits = struct.unpack_from('<HHIIHH', b, pos)
            assert kind in (3, 65534) and rate == 48000 and bits == 32 and stride == channels*4
        if tag == b'data':
            if n == 0xffffffff:
                n = size
            assert pos+n <= len(b) and channels is not None and n % (channels*4) == 0
            payload = b[pos:pos+n]
            return list(struct.unpack('<'+'f'*(n//4), payload)), payload
        pos += n+n%2
    raise AssertionError('Missing WAV data')

with tempfile.TemporaryDirectory(prefix='sc-mix-cli-', dir=root / '.cache') as tmp:
    base = Path(tmp)
    project = base / 'Séance — Ελλάδα'
    run(args.build / 'sc-project-tool', 'new', project)
    model = json.loads((project / 'project.json').read_text())
    source = [f32(((f % 53)-26)*.0625) for f in range(8192)]
    payload = struct.pack('<'+'f'*len(source), *source)
    data = b'RIFF'+struct.pack('<I', 36+len(payload))+b'WAVEfmt '+struct.pack('<IHHIIHH', 16, 3, 1, 48000, 192000, 4, 32)+b'data'+struct.pack('<I', len(payload))+payload
    path = project / 'media' / 'Échantillon.wav'
    path.parent.mkdir(exist_ok=True)
    path.write_bytes(data)
    asset_id = str(uuid.uuid4())
    model['assets'] = [{'id': asset_id, 'path': 'media/Échantillon.wav', 'sha256': hashlib.sha256(data).hexdigest(), 'sampleRate': 48000, 'layout': model['tracks'][0]['layout'], 'frames': 8192}]
    original = model['tracks'][0]
    tracks = []
    for n in range(32):
        t = copy.deepcopy(original)
        t['id'] = str(uuid.uuid4())
        t['name'] = f'Piste {n} — Łódź'
        t['processors'][0]['id'] = str(uuid.uuid4())
        for band in t['processors'][0]['bands']:
            band['id'] = str(uuid.uuid4())
            band['gainDb'] = 0
        t['clips'] = [{'id': str(uuid.uuid4()), 'assetId': asset_id, 'startFrame': n*7 % 128, 'sourceFrame': n*11 % 256, 'lengthFrames': 4096-n*3}]
        tracks.append(t)
    model['tracks'] = tracks
    model['exportRange'] = {'startFrame': 0, 'endFrame': 0}
    (project / 'project.json').write_text(json.dumps(model, ensure_ascii=False), encoding='utf-8')
    before = (project / 'project.json').read_bytes()
    result = json.loads(run(tool, 'render-mix', project, base / 'mix.wav', '--start', 113, '--end', 5000).stdout)
    actual, raw = wave(base / 'mix.wav')
    expected = []
    for f in range(113, 5000):
        total = 0.0
        for t in tracks:
            c = t['clips'][0]
            if c['startFrame'] <= f < c['startFrame']+c['lengthFrames']:
                total += source[c['sourceFrame']+f-c['startFrame']]
        expected.append(f32(total))
    assert actual == expected and result['frames'] == 4887 and result['channels'] == 1
    assert result['peak'] > 1 and result['over_full_scale_samples'] > 0
    assert hashlib.sha256(raw).hexdigest() == result['sample_sha256']
    assert hashlib.sha256((base / 'mix.wav').read_bytes()).hexdigest() == result['file_sha256']
    run(tool, 'render-mix', project, base / 'mix.wav', expected=1)
    replacement = json.loads(run(tool, 'render-mix', project, base / 'mix.wav', '--start', 113, '--end', 5000, '--rf64', '--replace-sha256', result['file_sha256']).stdout)
    assert replacement['replaced'] and replacement['rf64'] and wave(base / 'mix.wav')[0] == expected
    default = json.loads(run(tool, 'render-mix', project, base / 'default.wav').stdout)
    assert default['frames'] == max(t['clips'][0]['startFrame']+t['clips'][0]['lengthFrames'] for t in tracks)
    # A genuine active multi-track render must cancel before publishing any file.
    canceled = base / 'canceled.wav'
    child = subprocess.Popen([str(tool), 'render-mix', str(project), str(canceled), '--end', '3000000000'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        deadline = time.monotonic()+15
        writing = False
        while child.poll() is None and time.monotonic()<deadline:
            if any(p.stat().st_size >= 1048576 for p in base.glob('*.partial')):
                writing = True
                child.send_signal(signal.SIGTERM)
                break
            time.sleep(.005)
        assert writing, 'Multi-track job never entered write phase'
        stdout, stderr = child.communicate(timeout=15)
        assert child.returncode == 3, (child.returncode, stdout, stderr)
        assert not canceled.exists() and not list(base.glob('*.partial'))
    finally:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)
    # Incompatible layout is reported; no track or channel is silently omitted.
    model['tracks'][1]['layout'] = {'kind': 'stereo', 'channels': 2}
    model['tracks'][1]['clips'] = []
    (project / 'project.json').write_text(json.dumps(model, ensure_ascii=False), encoding='utf-8')
    run(tool, 'render-mix', project, base / 'incompatible.wav', expected=1)
    assert not (base / 'incompatible.wav').exists()
    (project / 'project.json').write_bytes(before)
    assert path.read_bytes() == data and (project / 'project.json').read_bytes() == before
    # Saved master deliberately includes two lanes with a stereo matrix; remaining lanes excluded explicitly.
    model = json.loads(before)
    tracks = model['tracks']
    model['master']={'id':str(uuid.uuid4()),'layout':{'kind':'stereo','channels':2},'outputIntent':{'backendId':'','portIdentity':'','ports':[]},'tracks':[
        {'trackId':tracks[0]['id'],'channels':[{'source':0,'destination':0,'gain':.5}]},
        {'trackId':tracks[1]['id'],'channels':[{'source':0,'destination':1,'gain':-.25}]}]}
    (project/'project.json').write_text(json.dumps(model,ensure_ascii=False),encoding='utf-8')
    master_before=(project/'project.json').read_bytes()
    master_result=json.loads(run(tool,'render-mix',project,base/'master.wav','--start',113,'--end',5000).stdout)
    master_samples=wave(base/'master.wav')[0]
    master_expected=[]
    for frame in range(113,5000):
        for t,gain in zip(tracks[:2],[.5,-.25],strict=True):
            c=t['clips'][0]
            value=source[c['sourceFrame']+frame-c['startFrame']] if c['startFrame']<=frame<c['startFrame']+c['lengthFrames'] else 0
            master_expected.append(f32(value*gain))
    assert master_result['channels']==2 and master_samples==master_expected
    saved_default=json.loads(run(tool,'render-mix',project,base/'master-default.wav').stdout)
    assert saved_default['frames']==max(t['clips'][0]['startFrame']+t['clips'][0]['lengthFrames'] for t in tracks[:2])
    assert saved_default['frames']<default['frames']
    assert (project/'project.json').read_bytes()==master_before and path.read_bytes()==data
    print(json.dumps({'tracks': 32, 'frames': 4887, 'oracle_difference': 0, 'rf64_and_confirmed_replace': True, 'active_sigterm': True, 'layout_rejection': True, 'source_project_preserved': True, 'audio_devices': False, 'saved_stereo_master_exact': True}))
