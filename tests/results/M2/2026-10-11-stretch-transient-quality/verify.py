#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recompute frozen onset/energy observations from bounded retained float PCM.

Standard library only. Does not rerender DSP, run native audio or qualify Q-STRETCH.
"""
from array import array
from fractions import Fraction
from pathlib import Path, PurePosixPath
import hashlib
import io
import json
import math
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
HEAD = 'd54dd878644b1075fb88b2f0eaa5609c4e39c775'
BUILD = '20a062dbf8449cd6e6c2b167b2ed12a3320e46f1'
sha = lambda b: hashlib.sha256(b).hexdigest()
from frozen_source import FrozenSource
provenance = FrozenSource(ROOT, (HEAD, BUILD))


def pcm(raw):
    assert 44 <= len(raw) <= 4194304 and raw[:4] in (b'RIFF', b'RF64') and raw[8:12] == b'WAVE'
    at, fmt, payload, extended = 12, None, None, None
    while at+8 <= len(raw):
        tag, size = raw[at:at+4], struct.unpack_from('<I', raw, at+4)[0]
        at += 8
        if tag == b'data' and size == 0xffffffff:
            assert extended is not None
            size = extended
        assert size <= len(raw)-at
        if tag == b'ds64':
            assert size >= 28 and extended is None
            extended = struct.unpack_from('<Q', raw, at+8)[0]
        elif tag == b'fmt ':
            assert fmt is None and size >= 16
            fmt = struct.unpack_from('<HHIIHH', raw, at)
            assert fmt[0] == 3 or (fmt[0] == 65534 and size >= 40 and raw[at+24:at+40] == bytes.fromhex('0300000000001000800000aa00389b71'))
            assert fmt[1] == 2 and fmt[4:] == (8, 32) and fmt[3] == fmt[2]*8
        elif tag == b'data':
            assert payload is None
            payload = raw[at:at+size]
        at += size+(size & 1)
    assert at == len(raw) and fmt is not None and payload is not None and len(payload) % 8 == 0
    values = array('f')
    assert values.itemsize == 4
    values.frombytes(payload)
    if sys.byteorder != 'little':
        values.byteswap()
    assert all(math.isfinite(v) for v in values)
    return values, fmt[2], payload


def detect(values, channel, event, rate, c):
    """Direct fsum of each causal window, independent of NumPy convolution."""
    center = Fraction(event).__floor__()
    count = len(values)//2
    first = max(0, center-int(rate*c['windowBeforeSeconds']))
    end = min(count, center+int(rate*c['windowAfterSeconds']))
    width = c['rmsFrames']
    rms = [math.sqrt(math.fsum(float(values[2*j+channel])**2 for j in range(max(0, k-width+1), k+1))/width) for k in range(first, end)]
    maximum = max(rms)
    threshold = maximum*c['thresholdFractionOfWindowMaximum']
    ba = max(first, center-int(rate*c['backgroundFirstSecondsBeforeEvent']))
    bb = max(ba, center-c['backgroundEndFramesBeforeEvent'])
    background = max(rms[ba-first:bb-first]) if bb > ba else None
    frame = next((first+k for k, value in enumerate(rms) if maximum > 0 and value >= threshold), None)
    reason = 'resolved'
    if frame is None:
        reason = 'no landmark'
    elif frame in (first, end-1):
        reason = 'window boundary'
    elif background is None or background >= threshold:
        reason = 'background already reaches event threshold'
    return {'resolved': reason == 'resolved', 'reason': reason, 'frame': frame, 'window': [first, end], 'windowMaximumRms': maximum, 'thresholdRms': threshold, 'backgroundMaximumRms': background}


def energy(values, channel, event, rate):
    center = Fraction(event).__floor__()
    a, b = max(0, center-rate//50-rate//1000), max(0, center-rate//1000)
    end = min(len(values)//2, center+rate//10)
    def rms(first, last):
        return math.sqrt(math.fsum(float(values[2*j+channel])**2 for j in range(first, last))/(last-first)) if last > first else None
    pre, body = rms(a, b), rms(center, end)
    db = 20*math.log10(pre/body) if pre and body else None
    return {'preWindow': [a, b], 'eventWindow': [center, end], 'preRms': pre, 'eventRms': body, 'preOverEventDb': db, 'silent': pre == 0, 'belowMinusFortyDb': pre == 0 or (db is not None and db <= -40)}


def compare(actual, recorded):
    assert set(actual) == set(recorded)
    for key, value in actual.items():
        if isinstance(value, float):
            assert math.isclose(value, recorded[key], rel_tol=1e-12, abs_tol=1e-12), (key, value, recorded[key])
        else:
            assert value == recorded[key], (key, value, recorded[key])


def main():
    s = json.loads((ROOT/'summary.json').read_bytes())
    raw = (ROOT/'observations.zip').read_bytes()
    assert len(raw) == s['captureBytes'] < 24*1024*1024 and sha(raw) == s['captureSha256']
    members = json.loads((ROOT/'members.json').read_bytes())
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        names = archive.namelist()
        assert len(names) == len(set(names)) == len(members) == s['captureMembers']
        assert set(names) == set(members) and sum(i.file_size for i in archive.infolist()) < 64*1024*1024
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            assert not path.is_absolute() and '..' not in path.parts and '\\' not in entry.filename and not entry.is_dir()
            assert entry.file_size <= 4194304 and not entry.flag_bits & 1
        def read(name):
            value = archive.read(name)
            assert members[name] == {'bytes': len(value), 'sha256': sha(value)}
            return value
        def js(name):
            return json.loads(read(name))
        inputs, contract = js('inputs.json'), js('contract.json')
        assert inputs['sourceCommit'] == s['sourceCommit'] == HEAD and inputs['originalBuildSource'] == BUILD
        assert provenance.roots[HEAD] == inputs['sourceTree']
        paths = provenance.files(HEAD)
        assert paths == set(inputs['files']) and len(paths) == s['sourceInputs'] == 309
        for path in paths:
            assert sha(provenance.read(HEAD,path)) == inputs['files'][path] == sha(provenance.read(BUILD,path))
        bindings = {'runnerSha256': 'experiments/stretch-transient-quality/run_bank.py', 'contractSha256': 'experiments/stretch-transient-quality/contract.json', 'sharedParsingHelperSha256': 'experiments/stretch-rate-quality/run.py', 'observerSha256': 'tests/worker_observation.py'}
        for key, path in bindings.items():
            assert inputs[key] == sha(provenance.read(HEAD,path))
        assert contract == json.loads(provenance.read(HEAD,'experiments/stretch-transient-quality/contract.json'))
        prior = js('prior-build-inputs.json')
        assert prior['sourceCommit'] == BUILD and inputs['workerSha256'] == prior['workerSha256'] and prior['files'] == inputs['files']
        assert b'Linking CXX executable sc-stretch-render-worker' in read('build.txt')
        read('configure.txt'); read('run-original.txt')
        sources, rows, processes, original = js('sources.json'), js('results.json'), js('processes.json'), js('summary.json')
        assert len(sources) == 9 and len(rows) == len(processes) == 36
        assert {(o['rate'], o['family']) for o in sources} == {(rate, family) for rate in contract['rates'] for family in contract['families']}
        assert {(r['rate'], r['family'], *r['ratio']) for r in rows} == {(rate, family, n, d) for rate in contract['rates'] for family in contract['families'] for n, d in contract['ratios']}
        expected = {'inputs.json', 'contract.json', 'sources.json', 'results.json', 'processes.json', 'summary.json', 'prior-build-inputs.json', 'run-original.txt', 'configure.txt', 'build.txt'}
        for source in sources:
            expected.add(source['relative'])
        for row in rows:
            expected.add(row['relativeAudio'])
            parent = str(PurePosixPath(row['relativeAudio']).parent)
            expected.update(parent+'/'+leaf for leaf in ('intent.json', 'complete.json'))
        assert set(names) == expected
        all_observations, unity, preecho_pass, preecho_fail = [], 0, 0, 0
        for source in sources:
            source_raw = read(source['relative'])
            assert sha(source_raw) == source['sha256']
            before, rate, source_payload = pcm(source_raw)
            assert rate == source['rate'] and len(before)//2 == source['frames'] == 2*rate+1 and sha(source_payload) == source['sampleSha256']
            assert source['events'] == [rate//2, rate, 3*rate//2]
            if source['family'] == 'impulse':
                for channel, (delay, gain) in enumerate(zip(contract['source']['channelDelayFrames'], contract['source']['channelGains'])):
                    assert [(k, before[2*k+channel]) for k in range(len(before)//2) if before[2*k+channel] != 0] == [(e+delay, gain) for e in source['events']]
            for row in (r for r in rows if (r['rate'], r['family']) == (rate, source['family'])):
                assert row['complete'] is True
                n, d = row['ratio']; request = row['request']
                assert request['sha256'] == source['sha256'] and request['rate'] == rate and request['channels'] == 2
                assert request['frames'] == request['sourceFrames'] == source['frames'] and [request['timeNumerator'], request['timeDenominator']] == [n, d]
                assert request['pitchMilliCents'] == 0 and request['contextEnabled'] is False and request['formantPreserved'] is False
                process = processes[row['processIndex']]
                assert process['request'] == request and process['exitCode'] == 0
                assert all(process[k] is False for k in ('observationTimedOut', 'outputLimited', 'retirementPending'))
                ready, receipt = map(json.loads, process['stdout'].splitlines())
                parent = str(PurePosixPath(row['relativeAudio']).parent)
                assert js(parent+'/complete.json') == receipt and receipt['complete'] is True
                intent = js(parent+'/intent.json')
                assert intent['complete'] is False and intent['operation'] == receipt['operation'] == ready['operation'] == request['operation']
                audio_raw = read(row['relativeAudio'])
                after, actual_rate, payload = pcm(audio_raw)
                assert actual_rate == rate and sha(audio_raw) == row['audioSha256'] == receipt['audioSha256']
                assert sha(payload) == row['sampleSha256'] == receipt['sampleSha256']
                assert len(after)//2 == row['frames'] == receipt['writtenFrames'] == (source['frames']*n+d-1)//d
                assert max(abs(v) for v in after) == row['peak']
                assert row['unityExact'] == (n == d)
                if n == d:
                    assert payload == source_payload
                    unity += 1
                assert len(row['observations']) == 3
                for event, recorded in zip(source['events'], row['observations']):
                    assert recorded['annotatedReferenceFrame'] == event and len(recorded['channels']) == 2
                    for channel, observed in enumerate(recorded['channels']):
                        landmark = event+contract['source']['channelDelayFrames'][channel]
                        target = Fraction(landmark*n, d)
                        src = detect(before, channel, landmark, rate, contract['detector'])
                        dst = detect(after, channel, target, rate, contract['detector'])
                        compare(src, observed['source']); compare(dst, observed['output'])
                        assert observed['channel'] == channel and observed['annotatedSourceFrame'] == landmark and observed['nominalOutputFrame'] == [target.numerator, target.denominator]
                        resolved = src['resolved'] and dst['resolved']
                        assert resolved == observed['resolved']
                        if resolved:
                            expected_frame = Fraction(src['frame']*n, d)
                            delta = Fraction(dst['frame'])-expected_frame
                            assert observed['calibratedExpectedFrame'] == [expected_frame.numerator, expected_frame.denominator]
                            assert observed['driftFrames'] == [delta.numerator, delta.denominator]
                            assert observed['driftMilliseconds'] == float(delta*1000/rate)
                            assert observed['timingPassed'] == (abs(delta)*1000 < rate)
                        else:
                            assert 'timingPassed' not in observed and 'driftFrames' not in observed
                        src_energy, dst_energy = energy(before, channel, landmark, rate), energy(after, channel, target, rate)
                        compare(src_energy, observed['sourceEnergy']); compare(dst_energy, observed['outputEnergy'])
                        passed = dst_energy['belowMinusFortyDb'] if source['family'] != 'attack-bed' else None
                        assert observed['silentFamilyPreechoPassed'] == passed
                        preecho_pass += passed is True; preecho_fail += passed is False
                        all_observations.append((source['family'], observed))
                    pair = recorded['channels']
                    assert recorded['physicalArrivalResolved'] == all(ch['resolved'] for ch in pair)
                    if recorded['physicalArrivalResolved']:
                        src_delay = pair[1]['source']['frame']-pair[0]['source']['frame']
                        out_delay = pair[1]['output']['frame']-pair[0]['output']['frame']
                        assert recorded['sourceCalibratedArrivalDifferenceFrames'] == src_delay and recorded['outputArrivalDifferenceFrames'] == out_delay and recorded['addedPhysicalArrivalDriftFrames'] == out_delay-src_delay
        resolved = [o for _, o in all_observations if o['resolved']]
        calculated = {'completedRenders': len(rows), 'attemptedRenders': len(rows), 'channelEventObservations': len(all_observations), 'resolvedTimingObservations': len(resolved), 'timingPasses': sum(o['timingPassed'] for o in resolved), 'timingFailures': sum(not o['timingPassed'] for o in resolved), 'unresolvedObservations': len(all_observations)-len(resolved), 'maximumResolvedDriftMilliseconds': max(abs(o['driftMilliseconds']) for o in resolved), 'unityExactCases': unity}
        for key, value in calculated.items():
            assert original[key] == s[key] == value
        for family in contract['families']:
            observations = [o for f, o in all_observations if f == family]
            assert original['byFamily'][family] == {'observations': len(observations), 'resolved': sum(o['resolved'] for o in observations), 'timingPassed': sum(o.get('timingPassed', False) for o in observations)}
        assert (calculated['timingFailures'], calculated['unresolvedObservations'], preecho_pass, preecho_fail) == (4, 160, s['silentFamilyPreechoPasses'], s['silentFamilyPreechoFailures']) == (4, 160, 36, 108)
        assert all(s[k] is False and original[k] is False and v is False for k, v in contract['scope'].items())
    print('Retained PCM independently audited: 36 renders; 56 resolved timing observations (52 pass, 4 fail), 160 unresolved; silent-family preecho 36 pass/108 fail; 9 unity identities. Q-STRETCH remains open.')


if __name__ == '__main__':
    main()
