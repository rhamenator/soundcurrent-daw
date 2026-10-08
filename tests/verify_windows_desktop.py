#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent retained-media oracle; never starts Windows or audio devices."""
from array import array
from pathlib import Path, PurePosixPath
import hashlib
import json
import math
import sys
from verify_input_acquisition import float_wav, require
from verify_windows_playback import coefficients, stereo_wav


def path(root, value):
    # A native fixture report can use Windows separators; portable project
    # media uses forward slashes. Normalize only after refusing drive/ADS names.
    p = PurePosixPath(value.replace('\\', '/'))
    require(not p.is_absolute() and '..' not in p.parts and ':' not in value and '\0' not in value,
            'Unsafe retained relative path')
    return root.joinpath(*p.parts)


def engine(raw, band, events):
    current = coefficients(band, 6)
    target = list(current)
    steps = [0.] * 5
    remaining = 0
    z1 = z2 = 0.
    by_frame = {e['frame']: e['gainDb'] for e in events}
    expected = array('f')
    for n, x in enumerate(raw):
        if n in by_frame:
            target = coefficients(band, by_frame[n])
            steps = [(b-a)/480 for a, b in zip(current, target)]
            remaining = 480
        if remaining:
            remaining -= 1
            current = [a+d for a, d in zip(current, steps)] if remaining else list(target)
        b0, b1, b2, a1, a2 = current
        y = b0*x + z1
        z1, z2 = b1*x - a1*y + z2, b2*x - a2*y
        if abs(z1) < 1e-30: z1 = 0.
        if abs(z2) < 1e-30: z2 = 0.
        expected.append(y)
    return expected


def verify(root):
    root = Path(root)
    report = json.loads((root/'probe.json').read_text(encoding='utf-8'))
    s = json.loads((root/'project.json').read_text(encoding='utf-8'))
    require(report['format'] == 'sc-wasapi-desktop-probe' and report['nativeWorkflowAccepted'] and
            report['rawUnchanged'] and report['defaultsUnchanged'] and
            report['cppAllocations'] == report['cppFrees'] == report['missingFrames'] == 0 and
            all(report[k] > 0 for k in ('recordingCallbacks','playbackCallbacks','observerCallbacks')),
            'Native desktop owner/audit refusal')
    require(s['sampleRate'] == 48000 and len(s['assets']) == len(s['tracks']) == 1,
            'Wrong native desktop project shape')
    asset = s['assets'][0]
    raw_path = path(root, report['rawPath'])
    tap_path = path(root/'loopback', report['capturePath'])
    export_path = path(root, report['exportPath'])
    require(report['rawPath'] == asset['path'] and
            hashlib.sha256(raw_path.read_bytes()).hexdigest() == asset['sha256'] == report['rawSha256'] and
            hashlib.sha256(tap_path.read_bytes()).hexdigest() == report['captureSha256'] and
            hashlib.sha256(export_path.read_bytes()).hexdigest() == report['exportSha256'],
            'Native desktop media identity differs')
    raw, exported = float_wav(raw_path), float_wav(export_path)
    left, right = stereo_wav(tap_path)
    # max()/sum() cannot serve as finiteness checks: a later NaN can leave an
    # earlier finite maximum unchanged. Refuse every channel before reductions.
    for name, samples in (('raw', raw), ('export', exported),
                          ('playback-left', left), ('playback-right', right)):
        require(all(math.isfinite(v) for v in samples),
                'Non-finite native '+name+' sample')
    require(len(raw) == len(exported) == asset['frames'] == report['frames'] == 480000 and
            len(left) == report['captureFrames'] and 470000 <= len(left) <= 500000,
            'Native desktop extent differs')
    if report.get('nonSilentPlaybackStartRequired'):
        require(any(abs(v) > .0001 for v in raw[:2]) and report['nativeStartupFrames'] > 0 and
                report['nativeStartupFrames'] == (report['devicePeriod100ns']*48000+9999999)//10000000,
                'Non-silent startup or admitted timing missing')
    for media, count in ((raw_path, len(raw)), (tap_path, len(left))):
        j = json.loads((media.parent/'journal.json').read_text(encoding='utf-8'))
        require(j['phase'] == 'finalized' and j['committedFrames'] == count and
                j['rejectedFrames'] == j['observedInvalidInputSamples'] == 0 and
                j['timingDomain'] == 'engine-frames' and j['timingOrigin']['backend'] == 4 and
                j['timingOrigin']['rateNumerator'] == 1 and
                j['timingOrigin']['rateDenominator'] == 48000 and
                not (media.parent/'first-fault.json').exists(), 'Native journal/origin/fault differs')
    # Independently regenerate the fixture source, including float32 operations.
    # Raw media must match a contiguous selected subset, never a fabricated tone.
    source = array('f')
    source.frombytes((root/'source-stereo.f32').read_bytes())
    if sys.byteorder != 'little': source.byteswap()
    require(len(source) == 576000*2, 'Wrong source extent')
    f32 = lambda v: array('f', [v])[0]
    seed = 0x753bdce1
    for n in range(576000):
        for c in range(2):
            seed = (seed*1664525+1013904223) & 0xffffffff
            expected = 0. if n < 12000 or n >= 450000 or 48000 <= n < 49024 else \
                f32(f32((seed >> 8)/16777216.-.5)*f32(.1))
            require(source[n*2+c] == expected, 'Retained source changed')
    source = source[::2]
    first = next((n for n, v in enumerate(raw) if abs(v) > .0001), len(raw))
    require(first < 96000, 'No nonzero native raw input')
    needle = raw[first:first+32]
    candidates = []
    for n in range(12000,96000):
        if abs(source[n]-needle[0]) <= 5e-5:
            error = max(abs(a-b) for a,b in zip(needle,source[n:n+32]))
            candidates.append((error,n-first))
    require(candidates, 'Raw source offset absent')
    error, offset = min(candidates)
    require(offset >= 0 and offset+len(raw) <= len(source) and error <= 5e-5,
            'Raw source alignment invalid')
    raw_error = max(abs(a-b) for a,b in zip(raw,source[offset:offset+len(raw)]))
    require(raw_error <= 5e-5, 'Native raw input lost/repeated/processed samples')
    bands = s['tracks'][0]['processors'][0]['bands']
    require(bands[0]['gainDb'] == 6 and all(b['gainDb'] == 0 for b in bands[1:]), 'Wrong EQ state')
    for key in ('recordingEvents','playbackEvents'):
        e = report[key]
        require(len(e) == 2 and [v['gainDb'] for v in e] == [-3,6] and
                24000 <= e[0]['frame'] < 96000 <= e[1]['frame'] < 240000 and
                0 < e[0]['revision'] < e[1]['revision'], 'Wrong live edit/Undo receipts')
    static = engine(raw,bands[0],[])
    require(all(math.isfinite(v) for v in static), 'Non-finite static EQ oracle')
    export_error = max(abs(a-b) for a,b in zip(static,exported))
    require(export_error <= 1e-7 and max(abs(a-b) for a,b in zip(raw,exported)) > .0001,
            'Reopened EQ export differs or bypassed processing')
    expected = engine(raw,bands[0],report['playbackEvents'])
    require(all(math.isfinite(v) for v in expected), 'Non-finite live EQ oracle')
    marker = next(n for n,v in enumerate(left) if abs(v) > .0001)
    expected_marker = next(n for n,v in enumerate(expected) if abs(v) > .0001)
    best = None
    for delay in range(marker-expected_marker-2,marker-expected_marker+3):
        begin,end = max(0,-delay), min(len(expected),len(left)-delay)
        if end-begin < 470000: continue
        x,y = expected[begin:end],left[begin+delay:end+delay]
        energy = sum(v*v for v in x)
        gain = sum(a*b for a,b in zip(x,y))/energy
        residual = max(abs(b-gain*a) for a,b in zip(x,y))
        if best is None or residual < best[0]: best=(residual,delay,gain,begin,end)
    require(best and .05 <= best[2] <= 1.1 and best[0] <= 5e-5,
            'Native GUI playback did not match live EQ/Undo')
    error,delay,gain,begin,end = best
    if 'nativeEndGuardFrames' in report:
        require(report['nativeEndGuardFrames'] == report['nativeStartupFrames'] and
                begin == 0 and end == len(expected), 'Guarded desktop source range not fully observed')
    require(all(abs(v*gain) <= 5e-5 for v in expected[:begin]) and
            all(abs(v*gain) <= 5e-5 for v in expected[end:]) and
            all(abs(v) <= 5e-5 for v in left[:begin+delay]) and
            all(abs(v) <= 5e-5 for v in left[end+delay:]) and
            max(abs(v) for v in right) <= 5e-5,
            'Unmatched non-silent audio or unselected output channel')
    return {'format':'sc-native-desktop-media-verification','frames':len(raw),
            'rawMaximumError':raw_error,'fixtureSourceOffsetFrames':offset,
            'exportMaximumError':export_error,'nativePlaybackMaximumResidual':error,
            'fixturePlaybackOffsetFrames':delay,'measuredPlaybackPathGain':gain,
            'matchedPlaybackFrames':end-begin,'unselectedChannelSilent':True,
            'oneTrackNativeDesktopWorkflowQualified':True,'installerQualified':False,
            'physicalLatencyOrSustainedSchedulingQualified':False,'nativeAudioReplayed':False}


if __name__ == '__main__':
    require(len(sys.argv) == 2, 'Supply retained native desktop project')
    print(json.dumps(verify(Path(sys.argv[1])),indent=2))
