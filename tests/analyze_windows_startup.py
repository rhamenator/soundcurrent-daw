#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Diagnose retained direct-render samples. No native replay or parity claim."""
from array import array
from pathlib import Path
import hashlib
import json
import math
import sys
from verify_input_acquisition import require
from verify_windows_desktop import path
from verify_windows_playback import stereo_wav


def samples(filename):
    require(filename.stat().st_size == 96000 * 2 * 4, 'Wrong retained source extent')
    values = array('f'); values.frombytes(filename.read_bytes())
    if sys.byteorder != 'little': values.byteswap()
    require(all(math.isfinite(v) for v in values), 'Nonfinite submitted/source sample')
    return values


def analyze(root):
    root = Path(root)
    report = json.loads((root / 'probe.json').read_text(encoding='utf-8-sig'))
    require(report['format'] == 'sc-wasapi-direct-startup-probe' and report['nativeSdkAccepted'] and
            report['sourceFrames'] == 96000 and report['rate'] == 48000 and
            report['silentLeadFrames'] in (0, 12000) and report['drained'] and
            report['cppAllocations'] == report['cppFrees'] == 0 and report['defaultsUnchanged'] and
            report['mixerOrEqInvoked'] is False and report['physicalOrSustainedTimingQualified'] is False,
            'Direct renderer owner/audit refusal')
    source = samples(root / 'source-stereo.f32')
    submitted = samples(root / 'submitted-stereo.f32')
    require(submitted == source, 'SDK lease differs from source intent')
    seed = 0x1a2b3c4d
    kind = report.get('sourceKind', 'noise')
    require(kind in ('noise','impulse'), 'Unknown direct source recipe')
    f32 = lambda v: array('f', [v])[0]
    for n in range(96000):
        seed = (seed * 1664525 + 1013904223) & 0xffffffff
        expected = 0. if n < report['silentLeadFrames'] or n >= 84000 else \
            f32(f32((seed >> 8) / 16777216. - .5) * f32(.1))
        if n == 0 and not report['silentLeadFrames']: expected = f32(.04)
        if kind == 'impulse' and n > 0: expected = 0.
        require(source[n * 2] == expected and source[n * 2 + 1] == 0., 'Source recipe differs')
    startup = report.get('startupFrames', 0)
    if 'startupFrames' in report:
        period, latency = report['devicePeriod100ns'], report['streamLatency100ns']
        require(0 < period <= 10000000 and 0 <= latency <= 10000000 and
                startup in (0, (period * 48000 + 9999999)//10000000) and
                0 <= startup <= report['bufferFrames'] and
                (not startup or report['silentLeadFrames'] == 0), 'Native startup timing admission differs')
    sequence = startup; content = clock = qpc = 0
    guard = report.get('endGuardSubmittedFrames',0)
    if 'endGuardFrames' in report:
        require(guard == report['endGuardFrames'] and guard in
                (0,(report['devicePeriod100ns']*48000+9999999)//10000000), 'Startup probe end guard differs')
    rows = report['observations']
    require(0 < len(rows) <= 256 and 0 < report['bufferFrames'] <= 32768, 'Unbounded native observations')
    for row in rows:
        require(row['submittedFrames'] == sequence and 0 < row['frames'] <= 2048 and
                row.get('contentSubmittedFrames', row['submittedFrames'] - startup) == content and
                row.get('startupFrames', 0) == startup and
                row['clockFrequency'] > 0 and row['clockPosition'] >= clock and row['qpc100ns'] >= qpc and
                0 <= row['paddingFrames'] <= report['bufferFrames'], 'Native queue/clock sequence differs')
        sequence += row['frames']; content += row['frames']; clock = row['clockPosition']; qpc = row['qpc100ns']
    require(sequence+guard == report['submittedFrames'] and 96000 <= content <= 98047, 'Submitted extent differs')
    capture = path(root / 'loopback', report['capturePath'])
    require(hashlib.sha256(capture.read_bytes()).hexdigest() == report['captureSha256'], 'Capture identity differs')
    left, right = stereo_wav(capture)
    require(len(left) == report['captureFrames'] and 94000 <= len(left) <= 120000,
            'Unexpected loopback extent')
    journal = json.loads((capture.parent / 'journal.json').read_text())
    require(journal['phase'] == 'finalized' and journal['committedFrames'] == len(left) and
            journal['rejectedFrames'] == journal['observedInvalidInputSamples'] == 0 and
            journal['timingOrigin']['backend'] == 4 and journal['timingDomain'] == 'engine-frames' and
            journal['timingOrigin']['rateNumerator'] == 1 and journal['timingOrigin']['rateDenominator'] == 48000 and
            not (capture.parent / 'first-fault.json').exists(),
            'Observer capture fault/origin differs')
    mono = source[::2]
    # Align using a nonperiodic interior segment, excluding the suspected startup.
    # This is fixture alignment, not production latency compensation.
    anchor = 0 if kind == 'impulse' else max(8192, report['silentLeadFrames'] + 2048)
    x = mono[anchor:anchor+128]; energy = sum(v*v for v in x)
    candidates = []
    for offset in range(0 if kind == 'impulse' else -2048, 2049):
        y = left[anchor+offset:anchor+offset+128]
        gain = sum(a*b for a, b in zip(x, y)) / energy
        error = max(abs(b-gain*a) for a, b in zip(x, y))
        if .05 <= gain <= 1.1: candidates.append((error, offset, gain))
    require(candidates, 'Direct source onset/interior absent')
    error, offset, gain = min(candidates)
    require(error <= 5e-5 and .05 <= gain <= 1.1, 'Direct native interior signal missing/altered')
    begin, end = max(0, -offset), min(96000, len(left)-offset)
    require(begin == 0 and end >= 84000, 'Non-silent source range missing')
    require(max(abs(v) for v in right) <= 5e-5 and
            all(abs(v) <= 5e-5 for v in left[:offset]) and
            all(abs(v) <= 5e-5 for v in left[end+offset:]), 'Unexpected observer signal')
    residual = [abs(left[n+offset]-gain*mono[n]) for n in range(end)]
    affected = [n for n, value in enumerate(residual) if value > 5e-5]
    blocks = []
    for first in range(0, 960, 48):
        x = mono[first:first+48]; y = left[first+offset:first+offset+48]
        energy = sum(v*v for v in x)
        blocks.append({'firstFrame': first, 'frames': 48,
                       'gainRelativeToInterior': sum(a*b for a,b in zip(x,y))/energy/gain if energy else None,
                       'maximumResidual': max(residual[first:first+48])})
    result = {'format': 'sc-wasapi-direct-startup-analysis', 'silentLeadFrames': report['silentLeadFrames'],
            'submittedLeaseEqualsIndependentSource': True, 'mixerOrEqInvoked': False,
            'fixtureOffsetFrames': offset, 'measuredInteriorGain': gain, 'matchedFrames': end,
            'first480MaximumResidual': max(residual[:480]),
            'after480MaximumResidual': max(residual[480:]),
            'directPathAlterationObserved': bool(affected),
            'firstAffectedFrame': affected[0] if affected else None,
            'lastAffectedFrame': affected[-1] if affected else None, 'startupBlocks': blocks,
            'physicalOrSustainedTimingQualified': False, 'driverOrOsCauseIsolated': False}
    if 'startupFrames' in report:
        result.update({'nativeStartupFrames': startup, 'devicePeriod100ns': period,
                       'streamLatency100ns': latency, 'projectSourceStartsAtNativeFrame': startup,
                       'sourceFrameZeroPreserved': not affected and report['silentLeadFrames'] == 0})
    if 'sourceKind' in report: result['sourceKind'] = kind
    if 'endGuardSubmittedFrames' in report: result['nativeEndGuardFrames'] = guard
    return result


if __name__ == '__main__':
    require(len(sys.argv) == 2, 'Supply retained direct-render root')
    print(json.dumps(analyze(Path(sys.argv[1])), indent=2))
