#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original onset fixtures and actual bounded v4 worker observations."""
import os
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
from pathlib import Path
from fractions import Fraction
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import time
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'stretch-rate-quality'))
from run import REPO, sha, pcm
from worker_observation import observe, diagnostic
import numpy as np

def fixture(rate, family, c):
    frames = 2*rate+1
    events = [rate//2, rate, 3*rate//2]
    audio = np.zeros((frames, 2), dtype=np.float64)
    time_axis = np.arange(frames, dtype=np.float64)/rate
    if family == 'attack-bed':
        audio += (.25*(np.sin(2*math.pi*440*time_axis)+.4*np.sin(2*math.pi*730*time_axis)))[:, None]
    for channel, (delay, gain) in enumerate(zip(c['source']['channelDelayFrames'], c['source']['channelGains'])):
        for event in events:
            if family == 'impulse':
                audio[event+delay, channel] += gain
            else:
                count = rate//10
                t = np.arange(count, dtype=np.float64)/rate
                values = gain*np.minimum(1., t/.001)*np.exp(-t/.02)*(.8*np.sin(2*math.pi*1000*t)+.4*np.sin(2*math.pi*2300*t))
                audio[event+delay:event+delay+count, channel] += values
    payload = audio.astype('<f4').tobytes()
    data = b'RIFF'+struct.pack('<I', 36+len(payload))+b'WAVEfmt '+struct.pack('<I', 16)+struct.pack('<HHIIHH', 3, 2, rate, rate*8, 8, 32)+b'data'+struct.pack('<I', len(payload))+payload
    return data, events

def detector(audio, channel, event, rate, c):
    event = Fraction(event)
    signal = audio[:, channel].astype(np.float64)
    rms = np.sqrt(np.maximum(0., np.convolve(signal*signal, np.ones(c['rmsFrames'])/c['rmsFrames'], mode='full')[:len(signal)]))
    center = event.numerator//event.denominator
    first = max(0, center-int(rate*c['windowBeforeSeconds']))
    end = min(len(signal), center+int(rate*c['windowAfterSeconds']))
    window = rms[first:end]
    maximum = float(window.max())
    threshold = maximum*c['thresholdFractionOfWindowMaximum']
    background_first = max(first, center-int(rate*c['backgroundFirstSecondsBeforeEvent']))
    background_end = max(background_first, center-c['backgroundEndFramesBeforeEvent'])
    background = rms[background_first:background_end]
    background_max = float(background.max()) if len(background) else None
    found = np.flatnonzero(window >= threshold) if maximum > 0 else []
    frame = int(found[0])+first if len(found) else None
    reason = 'resolved'
    if frame is None:
        reason = 'no landmark'
    elif frame in (first, end-1):
        reason = 'window boundary'
    elif background_max is None or background_max >= threshold:
        reason = 'background already reaches event threshold'
    return {'resolved': reason == 'resolved', 'reason': reason, 'frame': frame, 'window': [first, end], 'windowMaximumRms': maximum, 'thresholdRms': threshold, 'backgroundMaximumRms': background_max}

def rational(f):
    f = Fraction(f)
    return [f.numerator, f.denominator]

def energy(audio, channel, event, rate):
    center = Fraction(event).numerator//Fraction(event).denominator
    a, b = max(0, center-rate//50-rate//1000), max(0, center-rate//1000)
    d = min(len(audio), center+rate//10)
    def rms(v):
        return float(np.sqrt(np.mean(v.astype(np.float64)**2))) if len(v) else None
    pre, body = rms(audio[a:b, channel]), rms(audio[center:d, channel])
    db = 20*math.log10(pre/body) if pre and body else None
    return {'preWindow': [a, b], 'eventWindow': [center, d], 'preRms': pre, 'eventRms': body, 'preOverEventDb': db, 'silent': pre == 0, 'belowMinusFortyDb': pre == 0 or (db is not None and db <= -40)}

def observations(original, processed, rate, events, n, d, family, c):
    rows = []
    for event in events:
        channels = []
        for channel, delay in enumerate(c['source']['channelDelayFrames']):
            raw_event = event+delay
            source = detector(original, channel, raw_event, rate, c['detector'])
            target = Fraction(raw_event*n, d)
            output = detector(processed, channel, target, rate, c['detector'])
            row = {'channel': channel, 'annotatedSourceFrame': raw_event, 'nominalOutputFrame': rational(target), 'source': source, 'output': output, 'resolved': source['resolved'] and output['resolved']}
            if row['resolved']:
                delta = Fraction(output['frame'])-Fraction(source['frame']*n, d)
                row.update({'calibratedExpectedFrame': rational(Fraction(source['frame']*n, d)), 'driftFrames': rational(delta), 'driftMilliseconds': float(delta*1000/rate), 'timingPassed': abs(delta)*1000 < rate})
            source_energy, output_energy = energy(original, channel, raw_event, rate), energy(processed, channel, target, rate)
            row['sourceEnergy'] = source_energy
            row['outputEnergy'] = output_energy
            row['silentFamilyPreechoPassed'] = output_energy['belowMinusFortyDb'] if family != 'attack-bed' else None
            channels.append(row)
        group = {'annotatedReferenceFrame': event, 'channels': channels, 'physicalArrivalResolved': all(r['resolved'] for r in channels)}
        if group['physicalArrivalResolved']:
            raw_delay = channels[1]['source']['frame']-channels[0]['source']['frame']
            out_delay = channels[1]['output']['frame']-channels[0]['output']['frame']
            group.update({'sourceCalibratedArrivalDifferenceFrames': raw_delay, 'outputArrivalDifferenceFrames': out_delay, 'addedPhysicalArrivalDriftFrames': out_delay-raw_delay})
        rows.append(group)
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('worker', type=Path)
    parser.add_argument('prior_build_inputs', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    worker, output = args.worker.resolve(), args.output.resolve()
    assert not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=REPO)
    prior = json.loads(args.prior_build_inputs.read_bytes())
    assert prior['workerSha256'] == sha(worker.read_bytes())
    assert prior['files'] == {p: sha((REPO/p).read_bytes()) for p in prior['files']}, 'Rebuild after compiled source changes'
    contract_path = Path(__file__).parent/'contract.json'
    c = json.loads(contract_path.read_bytes())
    output.mkdir(parents=True, exist_ok=False)
    limits = c['limits']
    started = time.monotonic()
    rows, processes, sources = [], [], []
    def save(name, value):
        (output/name).write_text(json.dumps(value, indent=2, ensure_ascii=False)+'\n')
    inputs = {'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(), 'sourceTree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=REPO, text=True).strip(), 'originalBuildSource': prior['sourceCommit'], 'workerSha256': sha(worker.read_bytes()), 'files': prior['files'], 'runnerSha256': sha(Path(__file__).read_bytes()), 'contractSha256': sha(contract_path.read_bytes()), 'sharedParsingHelperSha256': sha((REPO/'experiments/stretch-rate-quality/run.py').read_bytes()), 'observerSha256': sha((REPO/'tests/worker_observation.py').read_bytes()), 'numpyVersion': np.__version__}
    save('inputs.json', inputs)
    save('contract.json', c)
    try:
        for rate in c['rates']:
            for family in c['families']:
                root = output/f'{family}-{rate}'
                (root/'media'/'derived').mkdir(parents=True)
                raw, events = fixture(rate, family, c)
                path = root/'media'/'tone-été.wav'
                path.write_bytes(raw)
                original, parsed_rate, digest, sample_digest = pcm(path, limits['maximumOutputBytes'])
                assert parsed_rate == rate and len(original) == 2*rate+1
                sources.append({'rate': rate, 'family': family, 'relative': str(path.relative_to(output)), 'sha256': digest, 'sampleSha256': sample_digest, 'events': events, 'frames': len(original)})
                for n, d in c['ratios']:
                    assert time.monotonic()-started < limits['bankSeconds']
                    request = {'protocol': 'sc-stretch-render-v4', 'processor': 'soundcurrent.stretch-positioned-copy-v1' if n == d else 'soundcurrent.stretch-rubberband4-r3-positioned-v2', 'operation': str(uuid.uuid4()), 'assetId': str(uuid.uuid4()), 'relative': 'media/tone-été.wav', 'sha256': digest, 'rate': rate, 'channels': 2, 'sourceFrames': len(original), 'first': 0, 'firstFraction': 0, 'firstDenominator': 1, 'frames': len(original), 'timeNumerator': n, 'timeDenominator': d, 'pitchMilliCents': 0, 'formantPreserved': False, 'contextEnabled': False, 'contextBefore': 0, 'contextAfter': 0}
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
                    record.update({'request': request, 'family': family})
                    processes.append(record)
                    row = {'rate': rate, 'ratio': [n, d], 'family': family, 'request': request, 'processIndex': len(processes)-1, 'complete': False}
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
                    processed, actual_rate, audio_sha, sample_sha = pcm(job/'audio.wav', limits['maximumOutputBytes'])
                    target = (len(original)*n+d-1)//d
                    assert len(processed) == target == receipt['writtenFrames'] and actual_rate == rate
                    assert audio_sha == receipt['audioSha256'] and sample_sha == receipt['sampleSha256']
                    measured = observations(original, processed, rate, events, n, d, family, c)
                    if n == d:
                        assert processed.tobytes() == original.tobytes()
                        assert all(ch['source'] == ch['output'] for event in measured for ch in event['channels'])
                    row.update({'complete': True, 'relativeAudio': str((job/'audio.wav').relative_to(output)), 'audioSha256': audio_sha, 'sampleSha256': sample_sha, 'frames': target, 'peak': float(np.max(np.abs(processed))), 'unityExact': n == d, 'observations': measured})
                    save('results.json', rows)
                    assert sum(p.stat().st_size for p in output.rglob('*') if p.is_file()) <= limits['maximumOwnedBytes']
                    channel_rows = [ch for event in measured for ch in event['channels']]
                    print(f'rate={rate} family={family} ratio={n}/{d} complete=true timing_pass={sum(ch.get("timingPassed", False) for ch in channel_rows)} timing_resolved={sum(ch["resolved"] for ch in channel_rows)} unresolved={sum(not ch["resolved"] for ch in channel_rows)}', flush=True)
        all_channels = [ch for r in rows if r['complete'] for event in r['observations'] for ch in event['channels']]
        resolved = [ch for ch in all_channels if ch['resolved']]
        by_family = {family: {'observations': sum(r['family'] == family for r in rows)*6, 'resolved': sum(ch['resolved'] for r in rows if r['complete'] and r['family'] == family for event in r['observations'] for ch in event['channels']), 'timingPassed': sum(ch.get('timingPassed', False) for r in rows if r['complete'] and r['family'] == family for event in r['observations'] for ch in event['channels'])} for family in c['families']}
        summary = {'format': 'sc-stretch-transient-quality-results-v1', 'sourceCommit': inputs['sourceCommit'], 'completedRenders': sum(r['complete'] for r in rows), 'attemptedRenders': len(rows), 'channelEventObservations': len(all_channels), 'resolvedTimingObservations': len(resolved), 'timingPasses': sum(ch['timingPassed'] for ch in resolved), 'timingFailures': sum(not ch['timingPassed'] for ch in resolved), 'unresolvedObservations': sum(not ch['resolved'] for ch in all_channels), 'maximumResolvedDriftMilliseconds': max(abs(ch['driftMilliseconds']) for ch in resolved), 'unityExactCases': sum(r.get('unityExact', False) for r in rows), 'byFamily': by_family, 'seconds': time.monotonic()-started, **c['scope']}
        save('summary.json', summary)
        print(json.dumps(summary), flush=True)
    finally:
        save('sources.json', sources)
        save('results.json', rows)
        save('processes.json', processes)
        assert inputs['files'] == {p: sha((REPO/p).read_bytes()) for p in inputs['files']}
        assert inputs['workerSha256'] == sha(worker.read_bytes())

if __name__ == '__main__':
    main()
