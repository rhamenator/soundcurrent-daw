#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay immutable observed evidence, then refuse altered waveform/clock/lease claims."""
from pathlib import Path
from array import array
import copy
import hashlib
import json
import tempfile
import zipfile
from verify_input_acquisition import check_trace, compare_waveforms, float_wav, source_wav, verify

ROOT = Path(__file__).resolve().parents[1]
CAPSULE = ROOT / 'tests/results/X007/2026-10-08-input-acquisition-observation.zip'


def rejected(operation):
    try:
        operation()
    except (ValueError, KeyError):
        return
    raise AssertionError('Altered evidence accepted')


def verify_capsule(archive, receipt):
    metadata = json.loads(Path(receipt).read_text())
    claim = metadata['archive']
    if Path(archive).stat().st_size != claim['bytes']:
        raise ValueError('Capsule size changed')
    if hashlib.sha256(Path(archive).read_bytes()).hexdigest() != claim['sha256']:
        raise ValueError('Capsule hash changed')
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        if len(names) != len(set(names)) or set(names) != set(metadata['entries']) or \
                len(names) != claim['logical_entries']:
            raise ValueError('Capsule manifest/member set changed')
        if z.testzip() is not None:
            raise ValueError('Capsule CRC failed')
        for name, expected in metadata['entries'].items():
            payload = z.read(name)
            if len(payload) != expected['bytes'] or hashlib.sha256(payload).hexdigest() != expected['sha256']:
                raise ValueError('Capsule payload changed: ' + name)


def main():
    refused = 0
    verify_capsule(CAPSULE, CAPSULE.with_suffix(".json"))
    with tempfile.TemporaryDirectory(prefix='sc-input-oracle-') as temp, zipfile.ZipFile(CAPSULE) as z:
        for label in ('direct', 'via-source', 'cmake-direct'):
            workspace = Path(temp) / label
            project = workspace / 'project-Δοκιμή'
            project.mkdir(parents=True)
            prefix = 'runs/' + label + '/'
            report = json.loads(z.read(prefix + 'project-Δοκιμή/probe.json'))
            asset = Path(report['assetPath'])
            assert not asset.is_absolute() and '..' not in asset.parts
            for name in ('source.wav', 'project-Δοκιμή/probe.json',
                         'project-Δοκιμή/handoffs.json', 'project-Δοκιμή/' + str(asset)):
                destination = workspace / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(z.read(prefix + name))
            result = verify(workspace)
            assert result['declared_silence_frames'] == 2048
            assert result['source_suffix_origin'] == (8192 if label == 'via-source' else 0)
            assert result['encoded_silent_frames_preserved'] == (0 if label == 'via-source' else 4096)
            if label != 'cmake-direct':
                continue
            raw = float_wav(project / asset)
            source = source_wav(workspace / 'source.wav')
            waveform = compare_waveforms(raw, source)
            trace = json.loads((project / 'handoffs.json').read_text())
            changes = [
                lambda r, t: r.update(firstFaultPresent=True),
                lambda r, t: r.update(writtenFrames=95999),
                lambda r, t: r.update(endReason=6),
                lambda r, t: r['audit'].update(allocations=1),
                lambda r, t: r['timingOrigin'].update(graphPosition=r['timingOrigin']['graphPosition'] + 1),
                lambda r, t: t.update(acquisition_version=1),
                lambda r, t: t['filters'][0].update(dropped=1),
                lambda r, t: t['filters'][0].update(unknown_queries=1),
                lambda r, t: t['filters'][0].update(outer_locks=1),
                lambda r, t: t['filters'][0]['rows'][1].update(position=t['filters'][0]['rows'][1]['position'] + 2048),
                lambda r, t: t['filters'][0]['rows'][1].update(cycle=t['filters'][0]['rows'][1]['cycle'] + 1),
                lambda r, t: t['filters'][0]['rows'][1].update(id=123456789),
                lambda r, t: t['filters'][0]['rows'][1].update(rate_denominator=44100),
                lambda r, t: t['filters'][0]['rows'][1].update(flags=1),
                lambda r, t: t['filters'][0]['rows'][1].update(nsec=-1),
                lambda r, t: t['filters'][0]['rows'][1].update(nsec=t['filters'][0]['rows'][0]['nsec']),
                lambda r, t: t['filters'][0]['rows'][1].update(nsec=t['filters'][0]['rows'][0]['nsec'] - 1),
                lambda r, t: t['filters'][0]['rows'][1].update(nsec=t['filters'][0]['rows'][1]['nsec'] + 1000000),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(acquisition_status=0),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(acquisition_status=2),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(api_suppressed=True),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(queue_matched=False),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(sdk_queues=2),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(queue_result=-1),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(chunk_bytes=4),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(chunk_stride=8),
                lambda r, t: t['filters'][0]['rows'][1]['queries'][0].update(chunk_flags=1),
                lambda r, t: t['filters'][0]['rows'][0]['queries'][0].update(returned=True),
                lambda r, t: t['filters'][0]['rows'][0]['queries'][0].update(chunk_flags=0),
            ]
            for change in changes:
                r, t = copy.deepcopy(report), copy.deepcopy(trace)
                change(r, t)
                rejected(lambda: check_trace(r, t, raw, waveform))
                refused += 1
            # These are actual media corruptions, not alternate valid source offsets.
            changed = array('f', raw)
            changed[30000] += .125
            rejected(lambda: compare_waveforms(changed, source)); refused += 1
            omitted = array('f', raw[:30000]) + raw[30001:] + array('f', [0])
            rejected(lambda: compare_waveforms(omitted, source)); refused += 1
            repeated = array('f', raw[:30000]) + raw[29999:-1]
            rejected(lambda: compare_waveforms(repeated, source)); refused += 1
            erased_silence = array('f', raw)
            erased_silence[2048 + 4096] = .1
            rejected(lambda: compare_waveforms(erased_silence, source)); refused += 1
            changed = array('f', raw); changed[0] = .1
            rejected(lambda: check_trace(report, trace, changed, waveform)); refused += 1
            # Extensible float parsing must reject wrong GUID and truncation.
            bytes_ = (project / asset).read_bytes()
            fmt = bytes_.index(b'fmt ')
            invalid = bytearray(bytes_); invalid[fmt + 8 + 24] = 1
            wrong = workspace / 'wrong-guid.wav'; wrong.write_bytes(invalid)
            rejected(lambda: float_wav(wrong)); refused += 1
            short = workspace / 'truncated.wav'; short.write_bytes(bytes_[:-1])
            rejected(lambda: float_wav(short)); refused += 1
            receipt = dict(report); receipt['assetPath'] = '../outside.wav'
            (project / 'probe.json').write_text(json.dumps(receipt))
            rejected(lambda: verify(workspace)); refused += 1
    with tempfile.TemporaryDirectory(prefix='sc-input-capsule-') as temp:
        temp = Path(temp)
        claim = json.loads(CAPSULE.with_suffix('.json').read_text())
        bad_receipt = temp / 'claim.json'
        claim['entries']['runs/direct/probe']['sha256'] = '0' * 64
        bad_receipt.write_text(json.dumps(claim))
        rejected(lambda: verify_capsule(CAPSULE, bad_receipt)); refused += 1
        claim = json.loads(CAPSULE.with_suffix('.json').read_text())
        claim['entries']['runs/cmake-direct/compiled-source/src/eq.cpp']['sha256'] = '0' * 64
        bad_receipt.write_text(json.dumps(claim))
        rejected(lambda: verify_capsule(CAPSULE, bad_receipt)); refused += 1
        claim = json.loads(CAPSULE.with_suffix('.json').read_text())
        claim['archive']['bytes'] += 1
        bad_receipt.write_text(json.dumps(claim))
        rejected(lambda: verify_capsule(CAPSULE, bad_receipt)); refused += 1
        claim = json.loads(CAPSULE.with_suffix('.json').read_text())
        claim['archive']['sha256'] = '0' * 64
        bad_receipt.write_text(json.dumps(claim))
        rejected(lambda: verify_capsule(CAPSULE, bad_receipt)); refused += 1
    print(f'Three relocated originals, all capsule payloads and timestamps pass; {refused} altered claims/media refused')


if __name__ == '__main__':
    main()
