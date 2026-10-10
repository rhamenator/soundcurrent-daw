#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect retained original PCM/measurements, without DSP or audio replay.
This is an evidence integrity check, not Q-STRETCH or native qualification.
Uses only the Python standard library and does not extract the archive.
"""
from pathlib import Path, PurePosixPath
from fractions import Fraction
import array
import cmath
import hashlib
import json
import math
import re
import struct
import sys
import zipfile

folder = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def near(actual, expected, message, tolerance=1e-10):
    check(math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance), message)


manifest = json.loads((folder / 'manifest.json').read_bytes())
archive = folder / 'capture.zip'
check(manifest['format'] == 'sc-region-quality-capture-v1', 'Capture format')
check(archive.stat().st_size == manifest['bytes'] < 16 * 1024 * 1024, 'Archive admission')
check(sha(archive.read_bytes()) == manifest['sha256'], 'Archive hash')
for key in ('nativeWindowsReplayed', 'nativeAudio', 'fullProcessingQuality', 'productBinaryUploaded', 'currentPackageReceipt'):
    check(manifest[key] is False, 'Unsupported scope: ' + key)
for key in ('all63FullRenderedWavesRetained', 'all37CropPairsRetained', 'allSixWholeToneWavesRetained', 'allFiveOriginalSourcesRetained'):
    check(manifest[key] is True, 'Missing PCM: ' + key)


def wave(data):
    check(len(data) < 8 * 1024 * 1024 and data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE', 'WAVE admission')
    at = 12
    data64 = None
    fmt = pcm = None
    while at + 8 <= len(data):
        tag = data[at:at + 4]
        size = struct.unpack_from('<I', data, at + 4)[0]
        at += 8
        if tag == b'data' and size == 0xffffffff:
            check(data64 is not None, 'Missing RF64 size')
            size = data64
        check(size <= len(data) - at, 'Truncated WAVE chunk')
        if tag == b'ds64':
            check(size >= 28, 'Short ds64')
            data64 = struct.unpack_from('<Q', data, at + 8)[0]
        if tag == b'fmt ':
            check(fmt is None and size >= 16, 'Invalid WAVE format')
            fmt = struct.unpack_from('<HHIIHH', data, at)
            encoding = fmt[0]
            if encoding == 65534:
                check(size >= 40 and data[at + 24:at + 40] == bytes.fromhex('0300000000001000800000aa00389b71'), 'Float subtype')
                encoding = 3
            check(encoding == 3 and fmt[1] in (2, 8) and fmt[2] == 48000 and fmt[5] == 32, 'Fixture format')
            check(fmt[3] == 48000 * fmt[1] * 4 and fmt[4] == fmt[1] * 4, 'Frame alignment')
        if tag == b'data':
            check(pcm is None, 'Repeated WAVE data')
            pcm = data[at:at + size]
        at += size + (size & 1)
    check(at == len(data) and fmt is not None and pcm is not None, 'WAVE chunks incomplete')
    check(len(pcm) % (4 * fmt[1]) == 0, 'Incomplete sample frame')
    samples = array.array('f')
    samples.frombytes(pcm)
    if sys.byteorder != 'little':
        samples.byteswap()
    check(samples.itemsize == 4 and all(math.isfinite(x) for x in samples), 'Finite IEEE samples')
    return samples, pcm, fmt[1]


def correlation(a, b, maximum):
    best = zero = None
    for lag in range(-maximum, maximum + 1):
        x, y = (a[lag:], b[:-lag]) if lag > 0 else (a[:lag], b[-lag:]) if lag < 0 else (a, b)
        if len(x) < 16:
            continue
        mx, my = math.fsum(x) / len(x), math.fsum(y) / len(y)
        x, y = [v - mx for v in x], [v - my for v in y]
        den = math.sqrt(math.fsum(v * v for v in x) * math.fsum(v * v for v in y))
        if den == 0:
            continue
        score = math.fsum(v * w for v, w in zip(x, y)) / den
        if lag == 0:
            zero = score
        if best is None or score > best[1] + 1e-12 or (abs(score - best[1]) <= 1e-12 and abs(lag) < abs(best[0])):
            best = lag, score
    return zero, best


def fft(values):
    """Radix-2 complex DFT, independent of NumPy's retained analysis."""
    data = list(map(complex, values))
    n = len(data)
    check(n > 0 and n & (n - 1) == 0, 'FFT power of two')
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            data[i], data[j] = data[j], data[i]
    size = 2
    while size <= n:
        step = cmath.exp(-2j * math.pi / size)
        half = size // 2
        for base in range(0, n, size):
            phase = 1 + 0j
            for k in range(half):
                a, b = data[base + k], phase * data[base + k + half]
                data[base + k], data[base + k + half] = a + b, a - b
                phase *= step
        size *= 2
    return data


with zipfile.ZipFile(archive) as z:
    infos = z.infolist()
    check(len(infos) == len({i.filename for i in infos}) == len(manifest['entries']) == 427, 'Member count')
    check(set(z.namelist()) == set(manifest['entries']) and sum(i.file_size for i in infos) < 48 * 1024 * 1024, 'Member admission')
    for info in infos:
        path = PurePosixPath(info.filename)
        check(not path.is_absolute() and '..' not in path.parts and '\\' not in info.filename and info.file_size < 8 * 1024 * 1024, 'Member path/budget')
        data = z.read(info)
        check(len(data) == manifest['entries'][info.filename]['bytes'] and sha(data) == manifest['entries'][info.filename]['sha256'], 'Member hash: ' + info.filename)
    get = lambda name: json.loads(z.read(name))
    summary, analysis, rows, processes = map(get, ('summary.json', 'analysis.json', 'results.json', 'processes.json'))
    check(summary['sourceCommit'] == 'e73eef65e7a4c16c7d949ef3aaffd24a0ef31dd0', 'Observed production base')
    check([summary[k] for k in ('cases', 'wholeJobs', 'actualWorkerProcesses', 'actualProbeProcesses', 'checks', 'callbackAudit')] == [37, 26, 63, 226, 1465, 0], 'Observed run counts')
    for key in ('nativeAudio', 'nativeWindowsReplayed', 'productionChanged', 'automaticContextQualified', 'fullProcessingQuality', 'QStretchPassed'):
        check(summary[key] is False, 'Run scope: ' + key)
    check(summary['unityContextCopyExact'] is True and summary['allSharedLiveExportExact'] is True, 'Shared reader observations')
    check(summary['seconds'] < summary['globalDeadlineSeconds'] == 240 and summary['perWorkerMemoryBytes'] == 256 * 1024 * 1024, 'Recorded budgets')
    build = get('logs/build.json')
    check(build['exitCode'] == 0 and build['applicationRebuilt'] is False and build['nativeWindowsBuilt'] is False, 'Standalone Linux build')
    check(z.read('logs/build.log') == b'', 'Final diagnostic build warnings')
    for name, value in [('probe.cpp', build['sourceSha256']), ('build.py', build['buildScriptSha256']), ('run.py', summary['runScriptSha256']), ('analyze.py', analysis['analysisScriptSha256']), ('rt_audit.cpp', build['auditSha256'])]:
        check(sha(z.read('experiment/' + name)) == value, 'Experimental source identity: ' + name)
    check(build['exeSha256'] == summary['probeSha256'], 'Probe executable identity')
    inputs = get('source-inputs.json')['inputs']
    check(len(inputs) == 288 and all(re.fullmatch('[0-9a-f]{64}', value) for value in inputs.values()), 'Retained production inputs')
    check(get('failures/initial-build/build.json')['exitCode'] == 1, 'Initial compile failure retained')
    check('cannot find -lsndfile' in z.read('failures/initial-build/build.log').decode(), 'Initial link diagnosis')
    failed = get('failures/run-1/processes.json')
    check(len(failed) == 4 and failed[-1]['exitCode'] == 1 and failed[-1]['mode'] == 'adopt', 'Initial harness failure')
    check('Media must reside in the media directory' in get('failures/run-1/failure.json')['error'], 'Initial export-path refusal')
    check(get('failures/retention-initial.json')['productionChanged'] is False, 'Initial archive cap refusal')
    workers = {p['request']['operation']: p for p in processes if p['kind'] == 'worker'}
    probes = [p for p in processes if p['kind'] == 'probe']
    check(len(workers) == 63 and len(probes) == 226 and len(processes) == 289, 'Actual process counts')
    for p in processes:
        check(p['pid'] > 0 and p['exitCode'] == 0 and p['stderr'] == '', 'Observed process exit')
    for operation, worker in workers.items():
        request = worker['request']
        check(worker['failure'] is None and worker['overflow'] is False and worker['seconds'] < 12, 'Worker outcome')
        replies = [json.loads(line) for line in worker['stdout'].splitlines()]
        ready, complete = replies
        prefix = 'renders/' + operation + '/'
        check(complete == get(prefix + 'complete.json') and complete['complete'] is True, 'Independent completion capture')
        check(z.read(prefix + 'start.request') == b'start\n' and ready['event'] == 'ready' and ready['renderKey'] == complete['renderKey'], 'Ready/start identity')
        local = [p for p in probes if p['root'] == worker['root'] and p['mode'] != 'crop']
        check([p['mode'] for p in local] == ['prepare', 'ready', 'adopt'], 'Parent process sequence')
        check(json.loads(local[0]['stdout']) == request and json.loads(local[1]['stdout'])['readyVerified'] is True, 'Parent approved request')
        check(processes.index(local[1]) < processes.index(worker) < processes.index(local[2]), 'Recorded ready/terminal/adoption order')
        adoption = json.loads(local[2]['stdout'])
        check(adoption['callbackAudit'] == 0 and adoption['sharedLiveExportExact'] and adoption['saveReopenUndoRedo'] and adoption['splitExact'], 'Recorded parent checks')
        for key in ('operation', 'assetId', 'first', 'firstFraction', 'firstDenominator', 'frames', 'rate', 'channels', 'pitchMilliCents', 'formantPreserved', 'contextBefore', 'contextAfter', 'timeNumerator', 'timeDenominator', 'protocol'):
            check(complete[key] == request[key], 'Request/completion binding: ' + key)
        full = request['frames'] + request['contextBefore'] + request['contextAfter']
        target = (full * request['timeNumerator'] + request['timeDenominator'] - 1) // request['timeDenominator']
        check(complete['target'] == complete['writtenFrames'] == target == adoption['fullOutputFrames'] and full == adoption['inputFrames'], 'Full-region geometry')
        intent = get(prefix + 'intent.json')
        check(intent['complete'] is False and intent['operation'] == operation, 'Intent identity')
        key = {k: v for k, v in intent.items() if k not in ('assetId', 'complete', 'operation', 'protocol', 'relative', 'sourceFrames')}
        check(sha(json.dumps(key, sort_keys=True, separators=(',', ':')).encode()) == complete['renderKey'], 'Canonical render key')
        data = z.read(prefix + 'audio.wav')
        samples, pcm, channels = wave(data)
        check(sha(data) == complete['audioSha256'] and sha(pcm) == complete['sampleSha256'], 'Complete WAV/PCM hashes')
        check(len(samples) // channels == target and channels == request['channels'], 'Rendered shape')
        near(max(map(abs, samples)), complete['peakLinear'], 'Rendered peak')
    raw = {}
    for name, digest in summary['sourceHashes'].items():
        data = z.read('sources/' + name)
        check(sha(data) == digest, 'Original source bytes')
        raw[name] = wave(data)
    exact = differences = 0
    for row in rows:
        check(row['id'] in range(37) and row['request']['operation'] in workers, 'Case identity')
        value, metrics = row['spec'], row['metrics']
        data, reference = [z.read(f"crops/{row['id']:02d}-{kind}.wav") for kind in ('context', 'whole')]
        a, pcm, channels = wave(data)
        b, ref_pcm, ref_channels = wave(reference)
        check(sha(data) == row['contextWaveSha256'] == row['adoption']['sha256'] and sha(reference) == row['referenceWaveSha256'] == row['reference']['sha256'], 'Crop byte hashes')
        n = (value['frames'] * value['timeNumerator'] + value['timeDenominator'] - 1) // value['timeDenominator']
        check(len(a) == len(b) == n * channels and channels == ref_channels == row['channels'], 'Crop geometry')
        check(row['request'] == workers[row['request']['operation']]['request'] and row['wholeRequest'] == workers[row['wholeRequest']['operation']]['request'], 'Whole/region request binding')
        for key in ('rate', 'channels', 'sha256', 'firstFraction', 'firstDenominator', 'pitchMilliCents', 'timeNumerator', 'timeDenominator', 'formantPreserved'):
            check(row['request'][key] == row['wholeRequest'][key], 'Matched diagnostic comparator')
        begin = Fraction(value['contextBefore'] * value['timeNumerator'], value['timeDenominator'])
        observed = row['adoption']['visibleBegin']
        check(begin == observed[0] + Fraction(observed[1], observed[2]), 'Fractional visible crop')
        difference = [float(x) - float(y) for x, y in zip(a, b)]
        rms = lambda samples: math.sqrt(math.fsum(x * x for x in samples) / len(samples))
        near(max(map(abs, difference)), metrics['maxSampleDifference'], 'Maximum sample difference')
        for name, data_values in [('differenceRms', difference), ('contextRms', a), ('referenceRms', b)]:
            near(rms(data_values), metrics[name], 'RMS: ' + name)
        if rms(b):
            near(rms(difference) / rms(b), metrics['relativeRmsDifference'], 'Relative RMS')
        is_exact = pcm == ref_pcm
        check(is_exact == metrics['exactFloatSampleBytes'], 'Exact crop PCM')
        exact += is_exact
        differences += not is_exact
        unity = value['timeNumerator'] == value['timeDenominator'] and value['pitchMilliCents'] == 0
        if unity:
            check(is_exact, 'Unity positioned copy')
            if not value['firstFraction']:
                source_pcm = raw[f"{row['family']}-{channels}.wav"][1]
                check(pcm == source_pcm[value['first'] * channels * 4:(value['first'] + value['frames']) * channels * 4], 'Integer unity raw bits/headroom/channel order')
        for cm in metrics['channelMetrics']:
            ch = cm['channel']
            x, y = list(a[ch::channels]), list(b[ch::channels])
            peak = max(range(n), key=lambda i: abs(x[i]))
            check(peak == cm['peakIndex'] and max(range(n), key=lambda i: abs(y[i])) == cm['referencePeakIndex'], 'Diagnostic peaks')
            near(max(map(abs, x)), cm['peakLinear'], 'Crop channel peak')
            check(sum(abs(v) > 1 for v in x) == cm['samplesAboveUnity'], 'Headroom observation')
            center = {0: 64, 16320: 16384, 32640: 32704}[value['first']] + 3 * ch
            nominal = (Fraction(center - value['first']) - Fraction(value['firstFraction'], value['firstDenominator'])) * Fraction(value['timeNumerator'], value['timeDenominator'])
            check(cm['nominalImpulseOrAttackFrame'] == [nominal.numerator, nominal.denominator], 'Nominal diagnostic landmark')
            zero, best = correlation(x, y, cm['contextReferenceCorrelation']['maximumLag'])
            recorded = cm['contextReferenceCorrelation']
            check((best[0] if best else None) == recorded['bestLag'], 'Diagnostic correlation lag')
            if best:
                near(best[1], recorded['bestScore'], 'Diagnostic correlation score')
            if zero is not None:
                near(zero, recorded['zeroLag'], 'Diagnostic zero-lag correlation')
            gain_sign = 1 if ch % 2 == 0 else -1
            zero, best = correlation([v * gain_sign for v in x], list(a[0::channels]), cm['channel0Correlation']['maximumLag'])
            recorded = cm['channel0Correlation']
            check((best[0] if best else None) == recorded['bestLag'], 'Channel-zero diagnostic lag')
            if best:
                near(best[1], recorded['bestScore'], 'Channel-zero diagnostic score')
            if zero is not None:
                near(zero, recorded['zeroLag'], 'Channel-zero zero-lag score')
            near(3 * ch * value['timeNumerator'] / value['timeDenominator'], cm['nominalChannelDelay'], 'Nominal channel displacement')
            energy = [v * v for v in x]
            total = math.fsum(energy)
            if total:
                near(math.fsum(i * v for i, v in enumerate(energy)) / total, cm['energyCentroid'], 'Crop energy centroid')
                cumulative = []
                running = 0
                for v in energy:
                    running += v
                    cumulative.append(running)
                quantiles = [next(i for i, v in enumerate(cumulative) if v >= total * q) for q in (.05, .5, .95)]
                check(quantiles == cm['energyQuantiles'], 'Crop energy quantiles')
    check(exact == analysis['exactUnityComparisons'] == 12 and differences == analysis['nonUnityDifferentComparisons'] == 25, 'Unity/non-unity comparisons')
    near(max(r['metrics']['maxSampleDifference'] for r in rows), analysis['maximumSampleDifference'], 'Aggregate difference')
    near(max(r['metrics']['relativeRmsDifference'] or 0 for r in rows), analysis['maximumRelativeRmsDifference'], 'Aggregate relative RMS')
    spectra = {}
    tones = analysis['steadyToneFrequencyObservations']
    check(len(tones) == 24 and len({t['operation'] for t in tones}) == 6, 'Frequency observation scope')
    for tone in tones:
        operation, channel = tone['operation'], tone['channel']
        if (operation, channel) not in spectra:
            data = z.read('tones/' + operation + '.wav')
            samples, pcm, channels = wave(data)
            check(sha(data) == tone['waveSha256'], 'Tone WAV hash')
            frames = len(samples) // channels
            length = 1 << ((frames // 2).bit_length() - 1)
            start = (frames - length) // 2
            segment = [float(samples[(start + i) * channels + channel]) * (.5 - .5 * math.cos(2 * math.pi * i / (length - 1))) for i in range(length)]
            spectra[operation, channel] = fft(segment), length
        spectrum, length = spectra[operation, channel]
        check(length == tone['analysisFrames'], 'Tone analysis length')
        pitch = workers[operation]['request']['pitchMilliCents']
        expected = tone['sourceHz'] * 2 ** (pitch / 1200000)
        near(expected, tone['expectedHz'], 'Tone target')
        band = [i for i in range(length // 2 + 1) if expected * .88 <= i * 48000 / length <= expected * 1.12]
        peak = max(band, key=lambda i: abs(spectrum[i]))
        logs = [math.log(max(abs(spectrum[i]), sys.float_info.min)) for i in (peak - 1, peak, peak + 1)]
        denominator = logs[0] - 2 * logs[1] + logs[2]
        offset = .5 * (logs[0] - logs[2]) / denominator if denominator else 0
        hz = (peak + offset) * 48000 / length
        near(hz, tone['measuredHz'], 'Independent FFT frequency', 1e-8)
        near(1200 * math.log2(hz / expected), tone['errorCents'], 'Independent cents error', 1e-7)
    near(max(abs(t['errorCents']) for t in tones), analysis['maximumSteadyToneFrequencyErrorCents'], 'Aggregate tone error')
    for key in ('wholeSourceIsAcousticGroundTruth', 'peakIsUniversalEventTimingOracle', 'QStretchPassed', 'QPitchPassed'):
        check(analysis[key] is False, 'Analysis scope: ' + key)
print(json.dumps({'retainedInspectionChecks': checks, 'fullRenderedWaves': 63, 'cropPairs': 37, 'toneObservations': 24,
                  'actualDSPReplayed': False, 'nativeWindowsReplayed': False, 'QStretchPassed': False, 'QPitchPassed': False}))
