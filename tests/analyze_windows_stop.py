#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only direct Stop/end diagnostic; altered samples never count as fidelity."""
from array import array
from pathlib import Path
import hashlib
import json
import sys
from analyze_windows_startup import samples
from verify_input_acquisition import require
from verify_windows_desktop import path
from verify_windows_playback import stereo_wav


def analyze(root):
    root = Path(root)
    r = json.loads((root/'probe.json').read_text(encoding='utf-8-sig'))
    cancel = r['mode'] == 'cancel-active'
    require(r['mode'] in ('cancel-active', 'non-silent-end') and
            r['format'] == 'sc-wasapi-direct-startup-probe' and r['nativeSdkAccepted'] and
            r['sourceKind'] == 'noise' and r['sourceFrames'] == 96000 and r['rate'] == 48000 and
            r['silentLeadFrames'] == 0 and r['drained'] is (not cancel) and
            r['cppAllocations'] == r['cppFrees'] == 0 and r['defaultsUnchanged'] and
            r['mixerOrEqInvoked'] is False and r['physicalOrSustainedTimingQualified'] is False,
            'Direct Stop owner/audit differs')
    period, latency, startup = r['devicePeriod100ns'], r['streamLatency100ns'], r['startupFrames']
    require(0 < period <= 10000000 and 0 <= latency <= 10000000 and
            startup == (period*48000+9999999)//10000000 and 0 < startup <= r['bufferFrames'] <= 32768,
            'Direct Stop startup timing differs')
    sequence = startup; content = clock = qpc = 0
    require(0 < len(r['observations']) <= 256, 'Unbounded Stop observations')
    for row in r['observations']:
        require(row['submittedFrames'] == sequence and row['contentSubmittedFrames'] == content and
                row['startupFrames'] == startup and 0 < row['frames'] <= 2048 and
                row['clockFrequency'] > 0 and row['clockPosition'] >= clock and row['qpc100ns'] >= qpc and
                0 <= row['paddingFrames'] <= r['bufferFrames'], 'Direct Stop queue/clock differs')
        sequence += row['frames']; content += row['frames']; clock = row['clockPosition']; qpc = row['qpc100ns']
    require(sequence == r['submittedFrames'] and content == r['contentSubmittedFrames'] == r['sourceCursor'] and
            (48000 <= content < 96000 if cancel else 96000 <= content <= 98047) and
            r['stopRequestedAtMinimumContentFrame'] == (48000 if cancel else 0), 'Stop extent differs')
    source, submitted = samples(root/'source-stereo.f32'), samples(root/'submitted-stereo.f32')
    seed = 0x1a2b3c4d
    f32 = lambda v: array('f', [v])[0]
    for n in range(96000):
        seed = (seed*1664525+1013904223)&0xffffffff
        expected = 0. if cancel and n >= 84000 else f32(f32((seed>>8)/16777216.-.5)*f32(.1))
        if n == 0: expected = f32(.04)
        require(source[n*2] == expected and source[n*2+1] == 0., 'Independent Stop source differs')
        require(submitted[n*2] == (expected if n < content else 0.) and submitted[n*2+1] == 0.,
                'Actual SDK lease sample differs from independent source')
    capture = path(root/'loopback', r['capturePath'])
    require(hashlib.sha256(capture.read_bytes()).hexdigest() == r['captureSha256'], 'Stop capture identity differs')
    left, right = stereo_wav(capture)
    require(len(left) == r['captureFrames'] and 32000 <= len(left) <= 110000, 'Stop capture extent differs')
    journal = json.loads((capture.parent/'journal.json').read_text())
    require(journal['phase'] == 'finalized' and journal['committedFrames'] == len(left) and
            journal['rejectedFrames'] == journal['observedInvalidInputSamples'] == 0 and
            journal['timingOrigin']['backend'] == 4 and journal['timingDomain'] == 'engine-frames' and
            journal['timingOrigin']['rateNumerator'] == 1 and journal['timingOrigin']['rateDenominator'] == 48000 and
            not (capture.parent/'first-fault.json').exists(), 'Stop observer fault/origin differs')
    mono = source[::2]; anchor = 8192; x = mono[anchor:anchor+128]; energy = sum(v*v for v in x)
    candidates = []
    for offset in range(0, 2049):
        y = left[anchor+offset:anchor+offset+128]
        gain = sum(a*b for a,b in zip(x,y))/energy
        if .05 <= gain <= 1.1:
            candidates.append((max(abs(b-gain*a) for a,b in zip(x,y)), offset, gain))
    require(candidates, 'Stop interior signal missing')
    error, offset, gain = min(candidates)
    require(error <= 5e-5, 'Stop interior altered')
    end = min(96000, content, len(left)-offset)
    require(end >= 32000 and all(abs(v) <= 5e-5 for v in right) and
            all(abs(v) <= 5e-5 for v in left[:offset]) and
            all(abs(v) <= 5e-5 for v in left[offset+end:]), 'Unexpected Stop observer signal')
    # Fitted gain is useful for alignment/diagnosis, not a license to normalize
    # an altered capture into fidelity. Qualification compares original samples
    # at unity gain, including a separate bound on uniform scaling.
    residual = [abs(left[n+offset]-mono[n]) for n in range(end)]
    affected = [n for n,v in enumerate(residual) if v > 5e-5]
    unity = abs(gain-1.) <= 1e-6
    fidelity = unity and not affected
    blocks = []
    for first in range(max(0,end-960), end, 48):
        last = min(first+48,end); x = mono[first:last]; y = left[first+offset:last+offset]
        energy = sum(v*v for v in x)
        blocks.append({'firstFrame':first,'frames':last-first,
                       'gainRelativeToInterior':sum(a*b for a,b in zip(x,y))/energy/gain if energy else None,
                       'maximumResidual':max(residual[first:last])})
    return {'format':'sc-wasapi-direct-stop-analysis','mode':r['mode'],
            'submittedLeaseEqualsIndependentSource':True,'mixerOrEqInvoked':False,
            'fixtureOffsetFrames':offset,'nativeStartupFrames':startup,'measuredInteriorGain':gain,
            'matchedFrames':end,'contentSubmittedFrames':content,'captureFrames':len(left),
            'maximumResidual':max(residual),'beforeFinal960MaximumResidual':max(residual[:-960]),
            'directPathAlterationObserved':not fidelity,'firstAffectedFrame':affected[0] if affected else None,
            'lastAffectedFrame':affected[-1] if affected else None,'finalBlocks':blocks,
            'unobservedSubmittedSourceFrames':min(content,96000)-end,
            'allSourceFramesObserved':end == 96000,'capturedPrefixFidelityQualified':fidelity,
            'fullNonSilentEndQualified':not cancel and end == 96000 and fidelity,
            'physicalOrSustainedTimingQualified':False,'driverOrOsCauseIsolated':False}


if __name__ == '__main__':
    require(len(sys.argv) == 2, 'Supply retained direct Stop root')
    print(json.dumps(analyze(Path(sys.argv[1])),indent=2))
