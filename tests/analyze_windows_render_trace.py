#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only post-EQ lease/loopback diagnosis; no replay or fidelity normalization."""
from array import array
from pathlib import Path
import hashlib
import json
import math
import sys
from verify_input_acquisition import require, float_wav
from verify_windows_playback import independent_engine, stereo_wav


def f32_file(path, limit):
    require(path.is_file() and 0 <= path.stat().st_size <= limit and path.stat().st_size % 4 == 0,
            'Render sample extent outside bound')
    values = array('f'); values.frombytes(path.read_bytes())
    if sys.byteorder != 'little': values.byteswap()
    require(all(math.isfinite(x) for x in values), 'Nonfinite render trace')
    return values


def analyze_trace(root, expected):
    require(expected and all(math.isfinite(v) for v in expected), 'Nonfinite or empty independent reference')
    root = Path(root); path = root/'render-trace.json'
    require(path.stat().st_size <= 4*1024*1024, 'Unbounded render metadata')
    r = json.loads(path.read_text(encoding='utf-8-sig'))
    require(r['format'] == 'sc-wasapi-render-trace-v1' and r['channels'] == 2 and
            r['sampleRate'] == 48000 and r['maximumFrames'] == 2048 and
            r['contentLeasesOnly'] is True and r['joined'] is True and
            r['malformed'] is False and r['lostRows'] == r['lostSampleFrames'] == 0,
            'Unjoined, malformed or lossy render trace')
    require(r['samplePath'] == 'render-lease-samples.f32', 'Unexpected render sample path')
    sample_path = root/r['samplePath']; data = f32_file(sample_path,32*1024*1024)
    require(len(data) == r['sampleValues'] and len(data) % 2 == 0 and
            hashlib.sha256(sample_path.read_bytes()).hexdigest() == r['sampleSha256'],
            'Render sample/hash extent differs')
    rows = r['observations']; require(0 < len(rows) <= 2048, 'Unbounded or empty render rows')
    offset = content = previous_clock = previous_qpc = 0
    startup = rows[0]['startupFrames']; require(0 <= startup <= 65536, 'Invalid native startup extent')
    error = 0.; acquire_failures = release_failures = aborts = 0
    committed = array('f'); terminated = False
    for sequence, row in enumerate(rows):
        fields = ('sequence','sampleOffsetValues','requestedFrames','copiedFrames','releasedFrames',
                  'acquireHresult','releaseHresult','action','submittedFrames','contentSubmittedFrames',
                  'startupFrames','clockPosition','clockFrequency','qpc100ns','paddingFrames')
        require(all(type(row[k]) is int for k in fields), 'Nonintegral render timing/extent')
        frames = row['requestedFrames']; action = row['action']
        require(not terminated and row['sequence'] == sequence and row['sampleOffsetValues'] == offset and
                0 < frames <= 2048 and action in (0,1,2) and
                row['contentSubmittedFrames'] == content and row['submittedFrames'] == startup+content and
                row['startupFrames'] == startup and row['clockFrequency'] > 0 and
                row['clockPosition'] >= previous_clock and row['qpc100ns'] >= previous_qpc and
                0 <= row['paddingFrames'] <= 65536, 'Render queue/clock/sample order differs')
        previous_clock, previous_qpc = row['clockPosition'], row['qpc100ns']
        require(-2147483648 <= row['acquireHresult'] <= 2147483647 and
                -2147483648 <= row['releaseHresult'] <= 2147483647, 'Invalid native HRESULT')
        if not row['acquired']:
            require(row['acquired'] is False and row['acquireHresult'] < 0 and
                    row['releaseObserved'] is False and row['releasedFrames'] == row['copiedFrames'] == 0 and
                    row['samplesComplete'] is False and action == 2, 'Failed acquisition became committed audio')
            acquire_failures += 1; terminated = True; continue
        require(row['acquired'] is True and row['acquireHresult'] == 0 and row['releaseObserved'] is True,
                'Acquired lease has no actual release result')
        if action == 2:
            require(row['releasedFrames'] == row['copiedFrames'] == 0 and row['samplesComplete'] is False,
                    'Unused abort backing was read or committed')
            aborts += 1; terminated = True
        else:
            require(row['samplesComplete'] is True and row['copiedFrames'] == row['releasedFrames'] == frames and
                    offset+frames*2 <= len(data), 'Truncated or unaccounted lease samples')
            for n in range(frames):
                reference = expected[content+n] if content+n < len(expected) else 0.
                error = max(error,abs(data[offset+n*2]-reference),abs(data[offset+n*2+1]))
            if row['releaseHresult'] >= 0:
                committed.extend(data[offset:offset+frames*2:2]); content += frames
            offset += frames*2
            if action == 1: terminated = True
        if row['releaseHresult'] < 0:
            release_failures += 1; terminated = True
    require(offset == len(data), 'Unindexed sample storage')
    return {'format':'sc-wasapi-render-trace-analysis-v1','leaseMaximumReferenceResidual':error,
            'leaseReferenceFidelity':error <= 1e-7,'committedContentFrames':content,
            'acquireFailures':acquire_failures,'releaseFailures':release_failures,'aborts':aborts,
            'nativeEndpointQualified':False}, committed


def compare_observer(committed, left, right, maximum_offset):
    # Interior fit finds alignment only. Fidelity uses original, unity-gain
    # samples; a fitted amplitude never repairs an altered Stop tail.
    anchor = 16384; end_anchor = min(anchor+256,len(committed))
    require(end_anchor > anchor, 'Insufficient committed interior for Stop diagnosis')
    x = committed[anchor:end_anchor]; energy = sum(v*v for v in x)
    require(energy > 0, 'No committed interior signal')
    require(type(maximum_offset) is int and 0 <= maximum_offset <= 131072,
            'Observer alignment outside admitted timing bounds')
    candidates = []
    for offset in range(min(maximum_offset,len(left)-end_anchor)+1):
        y = left[anchor+offset:end_anchor+offset]
        if len(y) != len(x): continue
        gain = sum(a*b for a,b in zip(x,y))/energy
        if .05 <= gain <= 1.1:
            candidates.append((max(abs(b-gain*a) for a,b in zip(x,y)),offset,gain))
    require(candidates, 'No matched observer interior')
    _, offset, gain = min(candidates)
    extent = min(len(committed),len(left)-offset)
    residual = [abs(left[n+offset]-committed[n]) for n in range(extent)]
    right_silent = all(abs(v) <= 5e-5 for v in right)
    outside_silent = all(abs(v) <= 5e-5 for v in left[:offset]) and all(abs(v) <= 5e-5 for v in left[offset+extent:])
    maximum = max(residual,default=0.)
    return {'fixtureOffsetFrames':offset,'measuredInteriorGain':gain,'comparedFrames':extent,
            'observerMaximumUnityResidual':maximum,'unobservedCommittedFrames':len(committed)-extent,
            'observerComparedLeaseFidelity':abs(gain-1.) <= 1e-6 and maximum <= 5e-5 and right_silent and outside_silent,
            'fullCommittedExtentObserved':extent == len(committed),'unselectedChannelSilent':right_silent}


def analyze_project(root):
    root = Path(root); probe = json.loads((root/'probe.json').read_text(encoding='utf-8-sig'))
    session = json.loads((root/'project.json').read_text(encoding='utf-8-sig'))
    require(probe['format'] == 'sc-wasapi-playback-probe' and probe['renderTracePath'] == 'render-trace.json' and
            probe['renderTraceComplete'] is True and session['sampleRate'] == 48000 and
            len(session['assets']) == len(session['tracks']) == 1 and probe['liveRevision'] == 17 and
            24000 <= probe['liveFrame'] < 48000,'Unexpected production EQ fixture')
    def owned_path(base,value):
        path = Path(value)
        require(not path.is_absolute() and '..' not in path.parts and '\\' not in value, 'Unsafe owned media path')
        return base/path
    asset = session['assets'][0]; raw_path = owned_path(root,asset['path'])
    require(hashlib.sha256(raw_path.read_bytes()).hexdigest() == asset['sha256'], 'Raw project hash differs')
    raw = float_wav(raw_path); require(len(raw) == 192000, 'Wrong production EQ source extent')
    bands = session['tracks'][0]['processors'][0]['bands']
    require(bands[0]['gainDb'] == 6 and all(b['gainDb'] == 0 for b in bands[1:]), 'Wrong independent EQ reference')
    expected = independent_engine(raw,bands[0],probe['liveFrame'])
    result, committed = analyze_trace(root,expected)
    guard = probe['endGuardSubmittedFrames']
    require(guard >= 0 and probe['submittedFrames'] == probe['startupFrames']+len(committed)+guard,
            'Final native submitted accounting differs')
    reference = f32_file(root/'expected-engine.f32',192000*4)
    require(len(reference) == len(expected) and max(abs(a-b) for a,b in zip(reference,expected)) <= 1e-7,
            'Independent EQ differs from retained engine reference')
    capture = owned_path(root/'loopback',probe['capturePath'])
    require(hashlib.sha256(capture.read_bytes()).hexdigest() == probe['captureSha256'], 'Observer hash differs')
    left,right = stereo_wav(capture); require(len(left) == probe['capturedFrames'], 'Observer extent differs')
    startup,buffer,latency = probe['startupFrames'],probe['bufferFrames'],probe['streamLatency100ns']
    require(type(startup) is int and type(buffer) is int and type(latency) is int and
            0 <= startup <= buffer <= 32768 and 0 <= latency <= 10000000,
            'Observer native preparation timing differs')
    # The DSP chunk limit does not bound native startup/observer alignment.
    # Include admitted startup, one endpoint bank and reported latency; this is
    # only a diagnostic search bound, never production recording compensation.
    maximum_offset = startup+buffer+(latency*48000+9999999)//10000000
    result.update(compare_observer(committed,left,right,maximum_offset))
    result['observerAlignmentSearchLimitFrames'] = maximum_offset
    result.update({'cancel':probe['cancel'],'actualFixtureAccepted':probe['nativeSdkAccepted'],
                   'observerFault':probe['captureFault'],'drained':probe['drained'],
                   'engineFrames':probe['engineFrames'],'submittedFrames':probe['submittedFrames'],
                   'fullPlaybackWorkflowQualified':False})
    return result


if __name__ == '__main__':
    require(len(sys.argv) == 2,'Supply retained production EQ playback project')
    print(json.dumps(analyze_project(sys.argv[1]),indent=2))
