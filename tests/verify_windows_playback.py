#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only native playback media oracle. No native audio replay or latency claim."""
from array import array
from pathlib import Path
import hashlib
import json
import math
import struct
import sys
from verify_input_acquisition import float_wav, require, MAX_BYTES


def stereo_wav(path):
    require(0 < path.stat().st_size <= MAX_BYTES, 'Loopback outside fixture bounds')
    data = path.read_bytes()
    require(data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE', 'Wrong loopback container')
    offset, size64, fmt, payload = 12, None, None, None
    while offset + 8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, offset); offset += 8
        if kind == b'data' and size == 0xffffffff:
            require(size64 is not None, 'Missing ds64'); size = size64
        require(offset + size <= len(data), 'Truncated loopback chunk')
        chunk = data[offset:offset + size]
        if kind == b'ds64':
            require(size >= 28 and size64 is None, 'Invalid ds64')
            size64 = struct.unpack_from('<Q', chunk, 8)[0]
        elif kind == b'fmt ':
            require(fmt is None and size >= 16, 'Invalid WAV format')
            fmt = struct.unpack_from('<HHIIHH', chunk)
            if fmt[0] == 0xfffe:
                require(size == 40 and struct.unpack_from('<HHI', chunk, 16) == (22, 32, 3) and
                        chunk[24:40] == bytes.fromhex('0300000000001000800000aa00389b71'),
                        'Wrong stereo extensible format')
                fmt = (3, *fmt[1:])
        elif kind == b'data':
            require(payload is None, 'Duplicate data'); payload = chunk
        offset += size + (size & 1)
    require(fmt == (3, 2, 48000, 384000, 8, 32) and payload and len(payload) % 8 == 0,
            'Expected stereo 48 kHz float loopback')
    values = array('f'); values.frombytes(payload)
    if sys.byteorder != 'little': values.byteswap()
    require(all(math.isfinite(v) for v in values), 'Nonfinite loopback')
    return values[::2], values[1::2]


def coefficients(band, gain):
    amplitude = 10 ** (gain / 40)
    omega = 2 * math.pi * band['frequencyHz'] / 48000
    alpha = math.sin(omega) / (2 * band['q'])
    denominator = 1 + alpha / amplitude
    return [(1 + alpha * amplitude) / denominator, -2 * math.cos(omega) / denominator,
            (1 - alpha * amplitude) / denominator, -2 * math.cos(omega) / denominator,
            (1 - alpha / amplitude) / denominator]


def independent_engine(raw, band, receipt):
    # Independently evaluated time-varying peaking recurrence. A 10 ms ramp
    # begins at the actual applied-frame receipt; all other fixture bands are
    # identity. This computes coefficients/state in Python, not through the engine.
    current, target = coefficients(band, 6), coefficients(band, -3)
    steps = [(b - a) / 480 for a, b in zip(current, target)]
    remaining = 0; z1 = z2 = 0.; expected = array('f')
    for n, x in enumerate(raw):
        if n == receipt: remaining = 480
        if remaining:
            remaining -= 1
            current = [a + d for a, d in zip(current, steps)] if remaining else list(target)
        b0, b1, b2, a1, a2 = current
        y = b0 * x + z1
        z1, z2 = b1 * x - a1 * y + z2, b2 * x - a2 * y
        if abs(z1) < 1e-30: z1 = 0.
        if abs(z2) < 1e-30: z2 = 0.
        expected.append(y)
    return expected


def verify(project):
    project = Path(project)
    report = json.loads((project / 'probe.json').read_text())
    session = json.loads((project / 'project.json').read_text())
    require(report['format'] == 'sc-wasapi-playback-probe' and report['nativeSdkAccepted'] and
            report['rawUnchanged'] and report['projectUnchanged'] and report['defaultsUnchanged'] and
            report['cppAllocations'] == report['cppFrees'] == report['missingFrames'] == 0 and
            not report['captureFault'] and report['nativeCaptureFailure'] is None and
            report['nativePlaybackFailure'] is None, 'Native owner/audit refusal')
    require(session['sampleRate'] == 48000 and len(session['assets']) == len(session['tracks']) == 1,
            'Wrong source project shape')
    asset = session['assets'][0]
    def relative(value):
        p = Path(value); require(not p.is_absolute() and '..' not in p.parts, 'Unsafe media path'); return p
    raw_path = project / relative(asset['path'])
    capture_path = project / 'loopback' / relative(report['capturePath'])
    require(hashlib.sha256(raw_path.read_bytes()).hexdigest() == asset['sha256'], 'Raw source hash differs')
    require(hashlib.sha256(capture_path.read_bytes()).hexdigest() == report['captureSha256'], 'Loopback hash differs')
    raw = float_wav(raw_path)
    left, right = stereo_wav(capture_path)
    minimum_match = 32000 if report['cancel'] else 48000
    require(len(raw) == 192000 and len(left) == report['capturedFrames'] and len(left) >= minimum_match,
            'Source/capture extent differs')
    require(all(math.isfinite(x) for x in raw) and any(x != 0 for x in left), 'No actual native signal')
    require(report['liveRevision'] == 17 and 24000 <= report['liveFrame'] < 48000, 'Wrong live receipt')
    bands = session['tracks'][0]['processors'][0]['bands']
    require(bands[0]['gainDb'] == 6 and all(b['gainDb'] == 0 for b in bands[1:]), 'Wrong EQ fixture')
    expected = independent_engine(raw, bands[0], report['liveFrame'])
    engine_bytes = (project / 'expected-engine.f32').read_bytes()
    require(len(engine_bytes) == 192000 * 4, 'Wrong retained engine reference extent')
    engine = array('f'); engine.frombytes(engine_bytes)
    if sys.byteorder != 'little': engine.byteswap()
    require(all(math.isfinite(x) for x in engine), 'Nonfinite engine reference')
    engine_error = max(abs(a-b) for a,b in zip(expected, engine))
    require(engine_error <= 1e-7, 'Independent live-ramp DSP differs')
    startup = report.get('startupFrames', 0)
    if 'startupFrames' in report:
        require(0 < report['devicePeriod100ns'] <= 10000000 and 0 <= report['streamLatency100ns'] <= 10000000 and
                startup == (report['devicePeriod100ns']*48000+9999999)//10000000 and
                startup <= report['bufferFrames'], 'Native startup timing differs')
    rows = report['observations']; sequence = startup; engine_frames = 0
    require(len(rows) == report['playerCallbacks'] and rows, 'Dropped/missing playback observations')
    previous_clock = previous_qpc = 0
    for row in rows:
        require(row['submittedFrames'] == sequence and row['engineStart'] == engine_frames and
                row.get('contentSubmittedFrames',sequence-startup) == sequence-startup and
                row.get('startupFrames',0) == startup and
                row['engineFrames'] <= row.get('nativeFrames',row['engineFrames']) <= 2048 and
                0 < row['engineFrames'] <= 2048 and row['clockFrequency'] > 0 and
                row['clockPosition'] >= previous_clock and row['qpc100ns'] >= previous_qpc and
                0 <= row['paddingFrames'] <= report['bufferFrames'], 'Timing/queue domains differ')
        sequence += row.get('nativeFrames',row['engineFrames']); engine_frames += row['engineFrames']
        previous_clock, previous_qpc = row['clockPosition'], row['qpc100ns']
    guard = report.get('endGuardSubmittedFrames',0)
    if 'endGuardFrames' in report:
        require(report['endGuardFrames'] in (0,startup) and
                guard == (0 if report['cancel'] else report['endGuardFrames']), 'Native tail guard differs')
    require(engine_frames == report['engineFrames'] and sequence+guard == report['submittedFrames'],
            'Final submitted/engine extent differs')
    if report['cancel']:
        require(report['status'] == 4 and not report['drained'] and 48000 <= engine_frames < 192000,
                'Cancellation incorrectly promoted to completion')
    else:
        require(report['status'] == 3 and report['drained'] and engine_frames == 192000 and
                len(left) == report['submittedFrames'],
                'Native range/drain/capture incomplete')
    # Fixture alignment only: find the known marker, never alter media or derive
    # production recording compensation from amplitude. Candidate offsets cover
    # a sample rounded to zero by a PCM endpoint; all retained samples are checked.
    first = next((n for n,x in enumerate(left) if abs(x) > 1e-5), len(left))
    best = None
    for offset in range(first - 12000 - 2, first - 12000 + 3):
        begin, end = max(0, -offset), min(len(expected), len(left) - offset)
        if end - begin < minimum_match: continue
        x = expected[begin:end]; y = left[begin+offset:end+offset]
        energy = sum(v*v for v in x)
        gain = sum(a*b for a,b in zip(x,y)) / energy if energy else 0
        error = max(abs(b - gain*a) for a,b in zip(x,y))
        if best is None or error < best[0]: best = (error, offset, gain, end - begin, begin, end)
    require(best and 0.05 <= best[2] <= 1.1 and best[0] <= 5e-5, 'Native signal lost/repeated/altered')
    begin, end, offset, gain = best[4], best[5], best[1], best[2]
    require(all(abs(x) <= 5e-5 for x in left[:begin+offset]) and
            all(abs(x) <= 5e-5 for x in left[end+offset:]), 'Unmatched capture carries unexpected audio')
    if not report['cancel']:
        require(all(abs(x*gain) <= 5e-5 for x in expected[:begin]) and
                all(abs(x*gain) <= 5e-5 for x in expected[end:]), 'Final non-silent source frames missing')
    require(max(abs(x) for x in right) <= 5e-5, 'Unselected native channel gained signal')
    return {'format':'sc-native-windows-playback-verification','cancel':report['cancel'],
            'engineFrames':engine_frames,'capturedFrames':len(left),'nativeQueueDrained':report['drained'],
            'independentEngineMaximumError':engine_error,'nativeMaximumResidual':best[0],
            'fixtureOffsetFrames':best[1],'measuredPathGain':best[2],'matchedFrames':best[3],
            'nativeClockUnitsDistinctFromQueueFrames':True,'unselectedChannelSilent':True,
            'GUIOrInstallerQualified':False,'PhysicalLatencyOrSustainedSchedulingQualified':False}


if __name__ == '__main__':
    require(len(sys.argv) == 2, 'Supply retained native playback project')
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
