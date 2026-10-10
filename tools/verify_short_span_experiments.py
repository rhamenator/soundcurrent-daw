#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check retained short-span observations; never replay or qualify current DSP."""
from pathlib import Path, PurePosixPath
import hashlib, json, math, struct, zipfile

root = Path(__file__).resolve().parents[1]
folder = root / 'tests/results/M2/2026-10-10-short-span-context'
sha = lambda data: hashlib.sha256(data).hexdigest()


def archive(kind):
    manifest = json.loads((folder / (kind + '-manifest.json')).read_bytes())
    path = folder / (kind + '-capture.zip')
    assert path.stat().st_size == manifest['bytes'] < 2 * 1024 * 1024
    assert sha(path.read_bytes()) == manifest['sha256']
    assert manifest['productionChanged'] is False and manifest['automaticContextQualified'] is False
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        assert len(infos) == len({i.filename for i in infos}) == len(manifest['entries'])
        assert sum(i.file_size for i in infos) < 32 * 1024 * 1024
        assert set(z.namelist()) == set(manifest['entries'])
        entries = {}
        for i in infos:
            name = PurePosixPath(i.filename)
            assert not name.is_absolute() and '..' not in name.parts and '\\' not in i.filename
            assert i.file_size < 4 * 1024 * 1024
            data = z.read(i)
            assert len(data) == manifest['entries'][i.filename]['bytes']
            assert sha(data) == manifest['entries'][i.filename]['sha256']
            entries[i.filename] = data
        return entries


def wave(data):
    assert data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE'
    at = 12
    fmt = pcm = sizes = None
    while at + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, at)
        at += 8
        if tag == b'data' and size == 0xffffffff:
            assert sizes is not None
            size = sizes[1]
        assert size <= len(data) - at
        chunk = data[at:at + size]
        if tag == b'ds64':
            sizes = struct.unpack_from('<QQQ', chunk)
        if tag == b'fmt ':
            assert fmt is None
            fmt = struct.unpack_from('<HHIIHH', chunk)
            assert fmt[0] == 3 or (fmt[0] == 65534 and chunk[24:40] == bytes.fromhex('0300000000001000800000aa00389b71'))
        if tag == b'data':
            assert pcm is None
            pcm = chunk
        at += size + (size & 1)
    assert at == len(data) and fmt and pcm is not None
    assert fmt[1] == 2 and fmt[2] == 48000 and fmt[4:] == (8, 32) and len(pcm) % 8 == 0
    samples = struct.unpack('<' + 'f' * (len(pcm) // 4), pcm)
    assert all(math.isfinite(v) for v in samples)
    return samples, pcm


context = archive('context')
result = json.loads(context['result.json'])
processes = json.loads(context['raw-processes.json'])
assert result['actualProcesses'] == len(processes) == 30
assert result['shortRefusals'] == 10 and result['completedContextOrWholeRenders'] == 20
assert result['rawUnchanged'] is True
for field in ('productionChange', 'automaticContextQualified', 'nativeWindowsReplayed', 'fullProcessingQuality', 'releaseUploaded'):
    assert result[field] is False
assert sha(context['owned/media/impulses.wav']) == result['rawSha256']
assert len(wave(context['owned/media/impulses.wav'])[0]) == 32768 * 2
assert len({p['pid'] for p in processes}) == 30
assert len(result['cases']) == 10
for index, case in enumerate(result['cases']):
    short, nearby, whole = processes[index * 3:index * 3 + 3]
    assert [p['kind'] for p in (short, nearby, whole)] == ['short', 'context', 'whole']
    assert short['exitCode'] != 0
    assert not any('/' + short['request']['operation'] + '/' in name for name in context)
    crops = []
    for p, start in ((nearby, case['contextCropStart']), (whole, case['wholeCropStart'])):
        request = p['request']
        assert p['exitCode'] == 0 and request['sha256'] == result['rawSha256']
        for field in ('timeNumerator', 'timeDenominator', 'pitchMilliCents'):
            assert request[field] == case[field]
        assert request['firstFraction'] == case['fraction'] and request['firstDenominator'] == case['fractionDenominator']
        prefix = 'owned/media/derived/' + request['operation'] + '/'
        marker = json.loads(context[prefix + 'complete.json'])
        assert marker == json.loads(p['stdout'].splitlines()[-1]) and marker['complete'] is True
        samples, pcm = wave(context[prefix + 'audio.wav'])
        assert sha(context[prefix + 'audio.wav']) == marker['audioSha256'] and sha(pcm) == marker['sampleSha256']
        assert len(samples) // 2 == marker['writtenFrames'] == marker['target']
        crop = samples[start * 2:(start + case['cropFrames']) * 2]
        assert len(crop) == case['cropFrames'] * 2
        crops.append(crop)
    differences = [a - b for a, b in zip(*crops)]
    assert (crops[0] == crops[1]) == case['contextVsWholeExact']
    assert max(map(abs, differences)) == case['maximumDifference']
    assert math.isclose(math.sqrt(sum(v * v for v in differences) / len(differences)), case['rmsDifference'], rel_tol=1e-12)

alignment = archive('alignment')
run = json.loads(alignment['run.json'])
summary = json.loads(alignment['analysis-summary.json'])
rows = [json.loads(line) for line in alignment['results.jsonl'].splitlines()]
assert len(rows) == 41
terminal = rows.pop()
assert terminal['summary'] is True and terminal['rawUnchanged'] is True
assert terminal['cases'] == 40 and terminal['actualRenders'] == 120
assert terminal['automaticContextQualified'] is False and terminal['fullProcessingQuality'] is False
assert terminal['nativeAudio'] is False and terminal['productionChanged'] is False
assert run['exitCode'] == 0 and run['pid'] > 0 and run['nativePlatform'] == 'linux'
for item in (run, summary):
    assert item['automaticContextQualified'] is False and item['fullProcessingQuality'] is False
assert run['nativeWindowsReplayed'] is False and run['productionChanged'] is False
assert len(rows) == summary['cases'] == 40 and summary['actualRenders'] == 120
assert len({(r['engine'], r['explicitEventAnchor'], r['fraction'], r['timeNumerator'], r['timeDenominator'], r['pitchMilliCents']) for r in rows}) == 40
assert sum(r['partition97Vs512']['exact'] for r in rows) == summary['partitionExactCases'] == 36
assert max(r['partition97Vs512']['maximumDifference'] for r in rows) == summary['maximumPartitionDifference']
stderr = alignment['stderr.log'].decode()
assert stderr.count('WARNING: draining:') == summary['warnings'] == 96
assert stderr.count('NOTE: ignoring key-frame') == summary['ignoredKeyframeMessages'] == 6
for name in ('probe.cpp', 'CMakeLists.txt'):
    assert sha(alignment['source/' + name]) == run['inputs']['experiments/stretch-alignment/' + name]
print('Retained short-span experiments verified: 30 process observations, 20 PCM/hash/crop comparisons, 40 alignment/partition metric rows. Historical observations only; no DSP/native replay or automatic-context qualification.')
