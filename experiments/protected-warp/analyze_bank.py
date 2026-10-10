#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Frozen numerical diagnostics. Copied cores do not qualify the processed gaps."""
import os
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
from pathlib import Path
from fractions import Fraction as F
import argparse, hashlib, json, math, struct
import numpy as np

p = argparse.ArgumentParser()
p.add_argument('folder', type=Path)
args = p.parse_args()
root = args.folder.resolve()
records = json.loads((root/'processes.json').read_text())
contract = json.loads((root/'contract.json').read_text())
rows, onsets, phases, cancellation, cores, preecho, boundaries = [], [], [], [], [], [], []
def pcm(path):
    data = path.read_bytes()
    assert len(data) < 4*1024*1024 and data[:4] == b'RIFF' and data[8:16] == b'WAVEfmt ' and data[36:40] == b'data'
    fmt = struct.unpack_from('<HHIIHH', data, 20)
    assert fmt[0] == 3 and fmt[2] == 48000 and fmt[5] == 32
    assert len(data) == 44+struct.unpack_from('<I', data, 40)[0]
    audio = np.frombuffer(data[44:], dtype='<f4').reshape(-1, fmt[1])
    assert np.isfinite(audio).all()
    return audio
def mapped(points, x, inverse=False):
    pts = [(F(v['source']), F(v['output'])) for v in points]
    if inverse:
        pts = [(b, a) for a, b in pts]
    if x == pts[-1][0]:
        return pts[-1][1]
    for (a, b), (c, d) in zip(pts, pts[1:]):
        if a <= x < c:
            return b+(x-a)*(d-b)/(c-a)
    raise AssertionError('Outside exact protected map')
def detector(audio, ch, event):
    signal = audio[:, ch].astype(np.float64)
    rms = np.sqrt(np.maximum(0, np.convolve(signal*signal, np.ones(32)/32, mode='full')[:len(signal)]))
    start, end = max(0, int(event)-3072), min(len(signal), int(event)+4096)
    window = rms[start:end]
    maximum = float(window.max())
    threshold = maximum*.1
    background = rms[max(start, int(event)-1024):max(start, int(event)-48)]
    backgroundMax = float(background.max()) if len(background) else None
    found = np.flatnonzero(window >= threshold) if maximum > 0 else []
    if not len(found):
        return {'resolved': False, 'reason': 'no landmark', 'window': [start, end]}
    frame = int(found[0])+start
    reason = 'resolved'
    if frame in [start, end-1]:
        reason = 'window boundary'
    elif backgroundMax is None or backgroundMax >= threshold:
        reason = 'background already reaches event threshold'
    return {'frame': frame, 'maximumWindowRms': maximum, 'thresholdRms': threshold, 'backgroundMaxRms': backgroundMax,
            'window': [start, end], 'resolved': reason == 'resolved', 'reason': reason}
def rms(audio):
    return float(np.sqrt(np.mean(audio.astype(np.float64)**2)))
for r in records:
    row = {'id': r['id'], 'request': r['request'], 'terminalSuccess': r['exitCode'] == 0, 'onsets': [], 'phase': [], 'cores': [], 'cancellation': [], 'boundaries': []}
    if r['exitCode'] != 0:
        row['failure'] = r.get('stderr', r.get('notStarted'))
        rows.append(row)
        continue
    folder = root/f"case-{r['id']:02d}"
    report = r['result']
    a, b = pcm(folder/'source.wav'), pcm(folder/'rendered.wav')
    channels, family, points = report['channels'], r['request']['family'], report['points']
    assert a.shape == (32768, channels) and b.shape == (report['target'], channels)
    for s in report['spans']:
        start, end = s['coreSourceBegin'], s['coreSourceEnd']
        outStart, outEnd = s['coreOutputBegin'], s['coreOutputEnd']
        value = {'case': r['id'], 'owner': s['owner'], 'sourceBegin': start, 'sourceEnd': end, 'outputBegin': outStart, 'outputEnd': outEnd,
                 'exactSampleBytes': a[start:end].tobytes() == b[outStart:outEnd].tobytes(),
                 'maximumSampleDifference': float(np.max(np.abs(a[start:end].astype(np.float64)-b[outStart:outEnd].astype(np.float64))))}
        cores.append(value)
        row['cores'].append(value)
        for label, sb, ob in [('start', s['sourceBegin'], s['outputBegin']), ('coreStart', start, outStart), ('coreEnd', end, outEnd), ('end', s['sourceEnd'], s['outputEnd'])]:
            sourceJump = float(np.max(np.abs(a[sb].astype(np.float64)-a[sb-1].astype(np.float64))))
            outputJump = float(np.max(np.abs(b[ob].astype(np.float64)-b[ob-1].astype(np.float64))))
            value = {'case': r['id'], 'owner': s['owner'], 'boundary': label, 'sourceFrame': sb, 'outputFrame': ob,
                     'sourceAdjacentSampleJump': sourceJump, 'outputAdjacentSampleJump': outputJump, 'listeningQualified': False}
            boundaries.append(value)
            row['boundaries'].append(value)
    if family != 'sustain':
        for event in contract['bank']['events']:
            target, group = mapped(points, F(event)), []
            for ch in range(channels):
                delay = 0 if family == 'cancellation' else 3*ch
                sourceDetector, outputDetector = detector(a, ch, event+delay), detector(b, ch, float(target)+delay)
                resolved = sourceDetector['resolved'] and outputDetector['resolved']
                entry = {'case': r['id'], 'event': event, 'channel': ch, 'sourceDetector': sourceDetector, 'outputDetector': outputDetector,
                         'resolved': resolved, 'expectedPhysicalDelayFrames': delay, 'timingDiagnosticPassed': False}
                if resolved:
                    bias = sourceDetector['frame']-event-delay
                    expected = mapped(points, F(event+bias))+delay
                    error = F(outputDetector['frame'])-expected
                    entry.update(sourceBiasFrames=bias, expectedDetectorFrame=[expected.numerator, expected.denominator], errorFrames=float(error), timingDiagnosticPassed=abs(error) < 48)
                group.append(entry)
                onsets.append(entry)
                sb, ob = event+delay, int(target)+delay
                rawBefore, outBefore = a[sb-1024:sb-48, ch], b[ob-1024:ob-48, ch]
                eventRms = rms(b[ob:ob+1536, ch])
                originalBaseline, outputBaseline = rms(rawBefore), rms(outBefore)
                ratio = 20*math.log10(outputBaseline/eventRms) if outputBaseline > 0 and eventRms > 0 else (-300. if eventRms > 0 else None)
                silent = family in ['impulse', 'attack', 'cancellation']
                preecho.append({'case': r['id'], 'event': event, 'channel': ch, 'originalBaselineRms': originalBaseline,
                                'outputBaselineRms': outputBaseline, 'outputExcessRms': outputBaseline-originalBaseline,
                                'eventRms': eventRms, 'preEventRatioDb': ratio, 'silentFloorApplicable': silent,
                                'diagnosticPassed': silent and ratio is not None and ratio <= -40})
            for entry in group:
                entry['arrivalDiagnosticPassed'] = False
                if entry['resolved'] and group[0]['resolved']:
                    delta = (entry['outputDetector']['frame']-group[0]['outputDetector']['frame'])-(entry['sourceDetector']['frame']-group[0]['sourceDetector']['frame'])
                    entry.update(arrivalOffsetChangeFrames=delta, arrivalDiagnosticPassed=delta == 0)
            row['onsets'].extend(group)
    if family == 'sustain':
        windows = [('core', i, s['coreSourceBegin'], s['coreSourceEnd'], s['coreOutputBegin'], s['coreOutputEnd']) for i, s in enumerate(report['spans'])]
        windows += [('gap', g['index'], g['sourceBegin'], g['sourceEnd'], g['outputBegin'], g['outputEnd']) for g in report['gaps']]
        gains = np.array([1, -.5, .25, -.125, .75, -.375, .1875, -.09375])[:channels]
        taper = np.hanning(2048)
        n = np.arange(2048)
        for scope, index, sb, se, ob, oe in windows:
            if min(se-sb, oe-ob) < 2048:
                continue
            center = (ob+oe)//2
            sourceCenter = mapped(points, F(center), True)
            outStart, srcStart = center-1024, int(sourceCenter)-1024
            assert srcStart >= sb and srcStart+2048 <= se and outStart >= ob and outStart+2048 <= oe
            for hz in [440., 730.]:
                carrier = np.exp(-2j*np.pi*hz*n/48000)
                z = np.sum(b[outStart:outStart+2048].astype(np.float64)*taper[:, None]*carrier[:, None], axis=0)*np.sign(gains)
                sz = np.sum(a[srcStart:srcStart+2048].astype(np.float64)*taper[:, None]*carrier[:, None], axis=0)*np.sign(gains)
                for ch in range(1, channels):
                    resolved = bool(min(abs(z[ch]), abs(z[0]), abs(sz[ch]), abs(sz[0])) > 1e-9)
                    change = float(np.angle(np.exp(1j*(np.angle(z[ch]/z[0])-np.angle(sz[ch]/sz[0])))))*180/math.pi if resolved else None
                    entry = {'case': r['id'], 'scope': scope, 'index': index, 'channel': ch, 'hz': hz, 'sourceStart': srcStart,
                             'outputStart': outStart, 'frames': 2048, 'resolved': resolved, 'phaseChangeDegrees': change,
                             'diagnosticPassed': resolved and abs(change) <= .0001}
                    phases.append(entry)
                    row['phase'].append(entry)
    if family == 'cancellation':
        windows = [('whole', 0, len(b))]+[('core', s['coreOutputBegin'], s['coreOutputEnd']) for s in report['spans']]+[('gap', g['outputBegin'], g['outputEnd']) for g in report['gaps']]
        for ch in range(0, channels, 2):
            assert np.max(np.abs(a[:, ch].astype(np.float64)+a[:, ch+1])) == 0
            for scope, begin, end in windows:
                maximum = float(np.max(np.abs(b[begin:end, ch].astype(np.float64)+b[begin:end, ch+1])))
                entry = {'case': r['id'], 'pair': [ch, ch+1], 'scope': scope, 'outputBegin': begin, 'outputEnd': end,
                         'maximumPairSum': maximum, 'diagnosticPassed': maximum <= 1e-6}
                cancellation.append(entry)
                row['cancellation'].append(entry)
    rows.append(row)
maximum = lambda values: max(values, default=None)
summary = {'format': 'sc-protected-warp-analysis-v1', 'analysisSourceSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
           'contractSha256': hashlib.sha256((root/'contract.json').read_bytes()).hexdigest(), 'numpyVersion': np.__version__, 'cases': rows, 'preecho': preecho,
           'coreObservations': len(cores), 'exactCoreObservations': sum(o['exactSampleBytes'] for o in cores),
           'onsetObservations': len(onsets), 'resolvedOnsets': sum(o['resolved'] for o in onsets), 'unresolvedOnsets': sum(not o['resolved'] for o in onsets),
           'timingDiagnosticPassed': sum(o['timingDiagnosticPassed'] for o in onsets),
           'maximumResolvedTimingErrorFrames': maximum(abs(o['errorFrames']) for o in onsets if o['resolved']),
           'maximumArrivalOffsetChangeFrames': maximum(abs(o['arrivalOffsetChangeFrames']) for o in onsets if 'arrivalOffsetChangeFrames' in o),
           'corePhaseObservations': sum(o['scope'] == 'core' for o in phases), 'corePhasePassed': sum(o['scope'] == 'core' and o['diagnosticPassed'] for o in phases),
           'gapPhaseObservations': sum(o['scope'] == 'gap' for o in phases), 'gapPhasePassed': sum(o['scope'] == 'gap' and o['diagnosticPassed'] for o in phases),
           'maximumGapPhaseChangeDegrees': maximum(abs(o['phaseChangeDegrees']) for o in phases if o['scope'] == 'gap' and o['resolved']),
           'cancellationObservations': len(cancellation), 'cancellationDiagnosticPassed': sum(o['diagnosticPassed'] for o in cancellation),
           'maximumWholeCancellationPairSum': maximum(o['maximumPairSum'] for o in cancellation if o['scope'] == 'whole'),
           'silentPreechoObservations': sum(o['silentFloorApplicable'] for o in preecho), 'silentPreechoPassed': sum(o['diagnosticPassed'] for o in preecho),
           'backgroundObservations': sum(not o['silentFloorApplicable'] for o in preecho), 'boundaryObservations': len(boundaries),
           'maximumOutputBoundaryJump': maximum(o['outputAdjacentSampleJump'] for o in boundaries),
           'fullQualityQualified': False, 'QStretchPassed': False, 'groupPhaseQualified': False, 'nativeAudio': False}
(root/'analysis.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps({k: v for k, v in summary.items() if k not in ['cases', 'preecho']}))
