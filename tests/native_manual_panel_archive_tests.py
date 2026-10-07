#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained native evidence and independently replay original canceled audio.

No extraction, native audio, third-party Python dependencies or original host paths.
The original failed run stays failed; this proves only its retained exact prefix.
"""
from array import array
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT / 'tests/results/M2/native-manual-observations/2026-10-07-native-manual-panel-evidence.zip'
RECEIPT = ROOT / 'tests/results/M2/2026-10-07-native-manual-desktop.json'


def rf64(raw):
    assert raw[:4] == b'RF64' and raw[8:12] == b'WAVE'
    offset, fmt, data_size = 12, None, None
    while offset + 8 <= len(raw):
        kind, size = struct.unpack_from('<4sI', raw, offset)
        offset += 8
        if kind == b'ds64':
            _, data_size, _ = struct.unpack_from('<QQQ', raw, offset)
        elif kind == b'fmt ':
            fmt = raw[offset:offset + size]
        elif kind == b'data':
            assert fmt is not None and data_size is not None
            code, channels, rate, _, align, bits = struct.unpack_from('<HHIIHH', fmt)
            assert (code == 3 or code == 0xfffe and struct.unpack_from('<I', fmt, 24)[0] == 3)
            assert rate == 48000 and bits == 32 and align == channels * 4
            assert data_size <= len(raw) - offset and data_size % align == 0
            values = array('f')
            values.frombytes(raw[offset:offset + data_size])
            if sys.byteorder != 'little':
                values.byteswap()
            return channels, values
        offset += size + size % 2
    raise AssertionError('Missing RF64 data')


def marker(at, channel):
    mask = (1 << 64) - 1
    value = at ^ (((channel + 1) * 0x9e3779b97f4a7c15) & mask)
    value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & mask
    value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & mask
    value ^= value >> 31
    return ((value >> 40) - 0x800000) * 2 ** -22


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def verify():
    receipt = json.loads(RECEIPT.read_text())
    expected_archive = receipt['evidence_archive']
    assert expected_archive['path'] == str(ARCHIVE.relative_to(ROOT))
    assert ARCHIVE.stat().st_size == expected_archive['bytes']
    assert hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == expected_archive['sha256']
    with zipfile.ZipFile(ARCHIVE) as z:
        assert z.testzip() is None
        def read(name):
            return json.loads(z.read(name))
        manifest = read('manifest.json')
        assert set(z.namelist()) == set(manifest['files']) | {'manifest.json'}
        for name, expected in manifest['files'].items():
            payload = z.read(name)
            assert len(payload) == expected['bytes']
            assert hashlib.sha256(payload).hexdigest() == expected['sha256'], name
        observations = manifest['observations']
        assert [r['observation'] for r in observations] == list(range(49, 72))
        for r in observations[:22]:
            assert r['defaults_unchanged'] and r['preexisting_links_unchanged'] and r['owned_nodes_removed']
        for n in range(66, 71):
            result = read(f'observations/{n}/project/native-manual-panel.json')
            assert result['passed'] and not result['physical_qualified'] and not result['windows_qualified'] and not result['sustained_qualified']
            for audit in result['callback_timing'].values():
                assert not audit['allocations'] and not audit['frees'] and not audit['blocking_locks'] and not audit['clock_overflow']
                timing = audit['timing']
                assert timing['complete_cpu_coverage'] and timing['complete_timing_coverage'] and timing['complete_thread_usage_coverage']
                assert timing['finite_deadline_thresholds_met']
            if n != 66:
                assert result['maximum_output_difference'] == 0 and result['output_peak'] > 1
        interrupted = read('observations/71/project/native-manual-panel.json')
        assert not interrupted['passed']
        fault = interrupted['owner_callback_fault']
        assert fault['status'] == 7 and fault['capacity'] == 256
        assert fault['received']['position'] - fault['previous']['position'] - fault['previous']['duration'] == 512
        assert interrupted['sink_state']['rejection_mask'] == 128
        assert interrupted['callback_timing']['owner']['invalid_acquisitions'] == 0
        source = interrupted['callback_timing']['source']['timing']
        assert source['maximum_clock']['cycle'] == fault['previous']['cycle'] + 1
        assert source['maximum_ns'] == 11065691 and source['maximum_callback_cpu_ns'] == 11061072
        assert not source['finite_deadline_thresholds_met']
        usage = source['maximum_callback_thread_usage']
        assert not any(usage[k] for k in ['minor_faults', 'major_faults', 'voluntary_switches', 'involuntary_switches'])
        rejected = read('observations/62/project/native-manual-panel.json')
        assert not rejected['passed']
        first = rejected['callback_timing']['sink']['first_invalid_buffer']
        assert first['seen'] and first['input'] and first['owned'] and first['data'] and first['aligned']
        assert first['requested'] == 256 and first['size'] == first['maxsize'] == 32768
        assert first['stride'] == 4 and first['chunk_flags'] == 2 and first['flags'] == 3
        # Independent replay of original53. Do not replace its original exit1.
        base = 'observations/53/project/'
        result = read(base + 'native-manual-panel.json')
        assert not result['passed']
        start = 137
        origin = result['first_owner_clock']['position']
        frames = result['callback_timing']['owner']['terminal_position'] - start
        assert frames == result['sink_state']['count']
        assert result['sink_state']['rejection_mask'] == 18
        assert result['sink_state']['failed_clock']['position'] == origin + frames
        model = read(base + 'project.json')
        # IDs/order are retained in the canonical saved project.
        tracks = model['tracks']
        lane_for_track = {track['id']: lane for lane, track in enumerate(tracks[1:])}
        monitor = None
        raw_samples = 0
        for name in z.namelist():
            if not name.startswith(base + 'media/') or not name.endswith('/journal.json'):
                continue
            journal = read(name)
            folder = name.removesuffix('journal.json')
            if journal['channels'] == 2:
                channels, monitor = rf64(z.read(folder + 'take.wav'))
                assert channels == 2 and len(monitor) == frames * 2
            elif journal['trackId'] in lane_for_track:
                lane = lane_for_track[journal['trackId']]
                latency = [41, 200, 0][lane]
                channels, values = rf64(z.read(folder + 'audio.partial.rf64'))
                assert channels == 1 and journal['inputLatencyFrames'] == latency
                position = journal['timingOrigin']['devicePosition']
                assert len(values) >= journal['committedFrames']
                for f in range(journal['committedFrames']):
                    assert values[f] == marker(position + f - latency, lane * 2 % 3)
                raw_samples += journal['committedFrames']
        assert raw_samples == 147000 and monitor is not None
        difference = 0
        for f in range(frames):
            file_sample = ((start + f) % 53 - 26) * .0625
            left = file_sample * .125 - marker(origin + f - 41, 0) + marker(origin + f, 1) * .5
            right = file_sample * -.25
            difference = max(difference, abs(monitor[2 * f] - float32(left)), abs(monitor[2 * f + 1] - float32(right)))
        assert difference == 0
        print(json.dumps({'archive_integrity': True, 'qualified_scopes': 5,
                          'original53_stays_failed': True, 'original53_raw_prefix_samples': raw_samples,
                          'original53_output_prefix_samples': frames * 2,
                          'original53_output_max_difference': difference, 'neutral_metadata62_retained': True}))


if __name__ == '__main__':
    verify()
