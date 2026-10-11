#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded actual-worker frequency/duration bank; no audio endpoint or VM."""
import os
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import time
import uuid
import numpy as np

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tests'))
from worker_observation import observe, diagnostic

def sha(data):
    return hashlib.sha256(data).hexdigest()

def pcm(path, maximum):
    data = path.read_bytes()
    assert len(data) <= maximum and data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE'
    at, size64, fmt, payload = 12, None, None, None
    while at + 8 <= len(data):
        tag, size = data[at:at+4], struct.unpack_from('<I', data, at+4)[0]
        at += 8
        if tag == b'data' and size == 0xffffffff:
            assert size64 is not None
            size = size64
        assert size <= len(data) - at
        if tag == b'ds64':
            assert size >= 28
            size64 = struct.unpack_from('<Q', data, at+8)[0]
        if tag == b'fmt ':
            assert fmt is None and size >= 16
            fmt = struct.unpack_from('<HHIIHH', data, at)
            assert fmt[0] == 3 or (fmt[0] == 65534 and size >= 40 and data[at+24:at+40] == bytes.fromhex('0300000000001000800000aa00389b71'))
            assert fmt[1] == 2 and fmt[5] == 32 and fmt[4] == 8
        if tag == b'data':
            assert payload is None
            payload = data[at:at+size]
        at += size + (size & 1)
    assert at == len(data) and fmt is not None and payload is not None and len(payload) % 8 == 0
    audio = np.frombuffer(payload, dtype='<f4').reshape(-1, 2)
    assert np.isfinite(audio).all()
    return audio, fmt[2], sha(data), sha(payload)

def frequency(audio, rate, channel, expected):
    n = 1 << ((len(audio)//2).bit_length()-1)
    first = (len(audio)-n)//2
    segment = audio[first:first+n, channel].astype(np.float64)
    spectrum = np.abs(np.fft.rfft(segment*np.hanning(n)))
    bins = np.fft.rfftfreq(n, 1/rate)
    band = np.flatnonzero((bins >= .88*expected) & (bins <= 1.12*expected))
    assert len(band) and band[0] > 0 and band[-1] < len(spectrum)-1
    peak = int(band[np.argmax(spectrum[band])])
    logs = np.log(np.maximum(spectrum[peak-1:peak+2], np.finfo(float).tiny))
    denominator = logs[0]-2*logs[1]+logs[2]
    offset = .5*(logs[0]-logs[2])/denominator if denominator else 0.
    assert abs(offset) <= 1
    measured = (peak+offset)*rate/n
    phase = np.arange(n, dtype=np.float64)*2*math.pi*expected/rate
    amplitude = float(2*abs(np.sum(segment*np.exp(-1j*phase)))/n)
    error = float(1200*math.log2(measured/expected))
    return {'channel': channel, 'expectedHz': expected, 'measuredHz': float(measured), 'errorCents': error, 'withinFiveCents': abs(error) <= 5, 'firstAnalysisFrame': first, 'analysisFrames': n, 'centralAmplitudeAtTarget': amplitude}

def parent_workflow(verifier, original_root, fresh_root, request, ready, timeout):
    """Each verifier creates a new project identity: give it a distinct root."""
    operation = request['operation']
    assert str(uuid.UUID(operation)) == operation
    assert request['relative'] == 'media/tone-été.wav'
    leaves = [request['relative']] + ['media/derived/'+operation+'/'+n for n in ['audio.wav', 'complete.json', 'intent.json']]
    originals = {}
    assert not fresh_root.exists()
    for relative in leaves:
        src = original_root/relative
        assert src.is_file() and not src.is_symlink() and src.stat().st_size <= 4194304
        raw = src.read_bytes()
        originals[relative] = {'bytes': len(raw), 'sha256': sha(raw)}
        dst = fresh_root/relative
        dst.parent.mkdir(parents=True, exist_ok=True)
        with dst.open('xb') as f:
            f.write(raw)
    assert originals[request['relative']]['sha256'] == request['sha256']
    p = subprocess.run([str(verifier), str(fresh_root), 'media/derived/'+operation+'/audio.wav', json.dumps(request), json.dumps(ready)], capture_output=True, text=True, timeout=timeout)
    assert len(p.stdout)+len(p.stderr) < 65536
    assert originals == {relative: {'bytes': (original_root/relative).stat().st_size, 'sha256': sha((original_root/relative).read_bytes())} for relative in leaves}
    for relative in [request['relative'], 'media/derived/'+operation+'/audio.wav']:
        assert originals[relative]['sha256'] == sha((fresh_root/relative).read_bytes())
    return p, originals

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('verifier', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    worker, verifier, output = args.worker.resolve(), args.verifier.resolve(), args.output.resolve()
    assert not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=REPO)
    contract = json.loads((Path(__file__).parent/'contract.json').read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    limits = contract['limits']
    started = time.monotonic()
    rows, originals, processes = [], [], []
    def save(name, value):
        (output/name).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n')
    paths = subprocess.check_output(['git', 'ls-files', 'include', 'src', 'ui', 'worker', 'tests', 'CMakeLists.txt'], cwd=REPO, text=True).splitlines()
    paths = [p for p in paths if p.endswith(('.cpp', '.hpp', '.c', '.h')) or p == 'CMakeLists.txt']
    inputs = {'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(), 'sourceTree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=REPO, text=True).strip(), 'workerSha256': sha(worker.read_bytes()), 'verifierSha256': sha(verifier.read_bytes()), 'files': {p: sha((REPO/p).read_bytes()) for p in paths}, 'runnerSha256': sha(Path(__file__).read_bytes()), 'contractSha256': sha((Path(__file__).parent/'contract.json').read_bytes()), 'numpyVersion': np.__version__}
    save('inputs.json', inputs)
    save('contract.json', contract)
    try:
        for rate in contract['rates']:
            root = output/f'rate-{rate}'
            (root/'media'/'derived').mkdir(parents=True)
            frames = 2*rate+1
            at = np.arange(frames, dtype=np.float64)/rate
            source = np.column_stack((1.8*np.sin(2*math.pi*1000*at), -1.35*np.sin(2*math.pi*1500*at))).astype('<f4')
            payload = source.tobytes()
            raw = b'RIFF'+struct.pack('<I', 36+len(payload))+b'WAVEfmt '+struct.pack('<I', 16)+struct.pack('<HHIIHH', 3, 2, rate, rate*8, 8, 32)+b'data'+struct.pack('<I', len(payload))+payload
            path = root/'media'/'tone-été.wav'
            path.write_bytes(raw)
            digest = sha(raw)
            originals.append({'rate': rate, 'relative': str(path.relative_to(output)), 'sha256': digest, 'frames': frames, 'calibration': [frequency(source, rate, ch, hz) for ch, hz in enumerate([1000., 1500.])]})
            for n, d in contract['ratios']:
                for pitch in contract['pitchMilliCents']:
                    assert time.monotonic()-started < limits['bankSeconds']
                    unity = n == d and pitch == 0
                    request = {'protocol': 'sc-stretch-render-v4', 'processor': 'soundcurrent.stretch-positioned-copy-v1' if unity else 'soundcurrent.stretch-rubberband4-r3-positioned-v2', 'operation': str(uuid.uuid4()), 'assetId': str(uuid.uuid4()), 'relative': 'media/tone-été.wav', 'sha256': digest, 'rate': rate, 'channels': 2, 'sourceFrames': frames, 'first': 0, 'firstFraction': 0, 'firstDenominator': 1, 'frames': frames, 'timeNumerator': n, 'timeDenominator': d, 'pitchMilliCents': pitch, 'formantPreserved': False, 'contextEnabled': False, 'contextBefore': 0, 'contextAfter': 0}
                    command = [str(worker), '--project-root', str(root), '--jobs-root', str(root/'media'/'derived'), '--memory-mib', str(limits['workerMemoryMiB']), '--maximum-input-frames', str(limits['maximumInputFrames']), '--maximum-output-bytes', str(limits['maximumOutputBytes']), '--deadline-ms', str(limits['workerDeadlineMilliseconds'])]
                    packets = []
                    def ready(line):
                        packet = json.loads(line)
                        packets.append(packet)
                        if packet.get('event') == 'ready':
                            assert packet['operation'] == request['operation']
                            (root/'media'/'derived'/request['operation']/'start.request').write_text('start\n')
                    result = observe(command, json.dumps(request), ready, timeout=limits['childSeconds'], maximum_output_bytes=65536)
                    record = json.loads(diagnostic(result))
                    record.update({'kind': 'worker', 'request': request})
                    processes.append(record)
                    row = {'rate': rate, 'ratio': [n, d], 'pitchMilliCents': pitch, 'request': request, 'processIndex': len(processes)-1, 'complete': False}
                    rows.append(row)
                    save('processes.json', processes)
                    save('results.json', rows)
                    assert sha(path.read_bytes()) == digest
                    if result.returncode != 0 or result.observation_timed_out or result.output_limited or result.retirement_pending:
                        continue
                    assert len(packets) == 2 and packets[-1]['complete'] is True
                    job = root/'media'/'derived'/request['operation']
                    receipt = json.loads((job/'complete.json').read_bytes())
                    assert receipt == packets[-1]
                    audio, actual_rate, audio_sha, sample_sha = pcm(job/'audio.wav', limits['maximumOutputBytes'])
                    target = (frames*n+d-1)//d
                    assert len(audio) == target == receipt['writtenFrames'] and actual_rate == rate
                    assert audio_sha == receipt['audioSha256'] and sample_sha == receipt['sampleSha256']
                    if unity:
                        assert audio.tobytes() == payload
                    observations = [frequency(audio, rate, ch, hz*2**(pitch/1200000)) for ch, hz in enumerate([1000., 1500.])]
                    row.update({'complete': True, 'relativeAudio': str((job/'audio.wav').relative_to(output)), 'audioSha256': audio_sha, 'sampleSha256': sample_sha, 'frames': target, 'peak': float(np.max(np.abs(audio))), 'floatHeadroom': bool(np.max(np.abs(audio)) > 1), 'unityExact': unity, 'frequency': observations})
                    if (n, d, pitch) == (3, 2, 1200000) or (rate, n, d, pitch) == (48000, 1, 2, -1200000):
                        fresh_root = output/'parents'/request['operation']
                        p, originals_copy = parent_workflow(verifier, root, fresh_root, request, packets[0], limits['childSeconds'])
                        processes.append({'kind': 'parent-verifier', 'operation': request['operation'], 'exitCode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr, 'originalsUnchanged': originals_copy})
                        row['parentWorkflowPassed'] = p.returncode == 0
                        assert sha(path.read_bytes()) == digest and sha((job/'audio.wav').read_bytes()) == audio_sha
                    save('results.json', rows)
                    save('processes.json', processes)
                    assert sum(p.stat().st_size for p in output.rglob('*') if p.is_file()) <= limits['maximumOwnedBytes']
                    print(f'rate={rate} ratio={n}/{d} pitch={pitch} complete=true maximum_frequency_error_cents={max(abs(o["errorCents"]) for o in observations):.6f}', flush=True)
        observations = [o for r in rows if r['complete'] for o in r['frequency']]
        summary = {'format': 'sc-stretch-rate-quality-results-v1', 'sourceCommit': inputs['sourceCommit'], 'completedRenders': sum(r['complete'] for r in rows), 'attemptedRenders': len(rows), 'frequencyObservations': len(observations), 'frequencyPasses': sum(o['withinFiveCents'] for o in observations), 'maximumFrequencyErrorCents': max(abs(o['errorCents']) for o in observations), 'unityExactCases': sum(r.get('unityExact', False) for r in rows), 'floatHeadroomCases': sum(r.get('floatHeadroom', False) for r in rows), 'parentWorkflowPasses': sum(r.get('parentWorkflowPassed', False) for r in rows), 'seconds': time.monotonic()-started, **contract['scope']}
        save('summary.json', summary)
        print(json.dumps(summary), flush=True)
    finally:
        save('originals.json', originals)
        save('results.json', rows)
        save('processes.json', processes)
        assert inputs['files'] == {p: sha((REPO/p).read_bytes()) for p in paths}
        assert inputs['workerSha256'] == sha(worker.read_bytes()) and inputs['verifierSha256'] == sha(verifier.read_bytes())

if __name__ == '__main__':
    main()
