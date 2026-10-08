#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only oracle for an owned single-track input-acquisition experiment.

Amplitude is examined only to compare this known fixture waveform. This is not
an input-admission rule, a silence trimmer, or an alignment correction algorithm.
No external Python dependency; no native audio API or project mutation.
"""
from array import array
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys
import wave

MAX_BYTES = 64 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def float_wav(path):
    require(0 < path.stat().st_size <= MAX_BYTES, 'Take size outside fixture bounds')
    data = path.read_bytes()
    require(len(data) >= 12 and data[:4] in (b'RIFF', b'RF64') and data[8:12] == b'WAVE',
            'Unsupported take container')
    offset, data64, fmt, payload = 12, None, None, None
    while offset + 8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        if kind == b'data' and size == 0xffffffff:
            require(data64 is not None, 'RF64 take missing ds64')
            size = data64
        require(offset + size <= len(data), 'Truncated WAV chunk')
        chunk = data[offset:offset + size]
        if kind == b'ds64':
            require(size >= 28 and data64 is None, 'Invalid/duplicate ds64')
            data64 = struct.unpack_from('<Q', chunk, 8)[0]
        elif kind == b'fmt ':
            require(fmt is None and size >= 16, 'Invalid/duplicate WAV format')
            fmt = struct.unpack_from('<HHIIHH', chunk)
            if fmt[0] == 0xfffe:
                require(size == 40 and struct.unpack_from('<HHI', chunk, 16) == (22, 32, 4) and
                        chunk[24:40] == bytes.fromhex('0300000000001000800000aa00389b71'),
                        'Unsupported extensible format')
                fmt = (3, *fmt[1:])
        elif kind == b'data':
            require(payload is None, 'Duplicate WAV data')
            payload = chunk
        offset += size + (size & 1)
    require(fmt == (3, 1, 48000, 192000, 4, 32) and payload is not None,
            'Expected mono 48kHz IEEE float take')
    require(len(payload) % 4 == 0, 'Partial float sample')
    samples = array('f')
    samples.frombytes(payload)
    if sys.byteorder != 'little':
        samples.byteswap()
    return samples


def source_wav(path):
    require(0 < path.stat().st_size <= MAX_BYTES, 'Source outside fixture bounds')
    with wave.open(str(path), 'rb') as source:
        require(source.getnchannels() == 1 and source.getsampwidth() == 2 and
                source.getframerate() == 48000 and source.getcomptype() == 'NONE',
                'Expected mono 48kHz PCM16 source')
        count = source.getnframes()
        samples = array('h')
        samples.frombytes(source.readframes(count))
    if sys.byteorder != 'little':
        samples.byteswap()
    require(len(samples) == count, 'Truncated source')
    return [value / 32768 for value in samples]


def compare_waveforms(raw, source):
    require(len(raw) == 96000, 'Recording target incomplete')
    prefix = next((index for index, value in enumerate(raw) if value != 0), len(raw))
    require(prefix < len(raw) and len(raw) - prefix >= 64, 'No known-source marker')
    needle = list(raw[prefix:prefix + 64])
    matches = [index for index, value in enumerate(source) if value == needle[0] and
               source[index:index + 64] == needle]
    require(len(matches) == 1, 'Source marker missing/ambiguous')
    origin = matches[0]
    require(list(raw[prefix:]) == source[origin:origin + len(raw) - prefix],
            'Raw suffix differs: missing/repeated/altered source samples')
    return {'zero_prefix_frames': prefix, 'source_suffix_origin': origin,
            'every_suffix_sample_exact': True,
            'encoded_silent_frames_preserved': sum(value == 0 for value in raw[prefix:])}


def check_trace(report, trace, raw, waveform):
    require(report['format'] == 'sc-owned-input-acquisition-observation' and
            report['schemaMajor'] == 1 and report['status'] == 2 and
            report['endReason'] == 2 and not report['firstFaultPresent'] and
            report['rejectedFrames'] == 0 and report['storageDiagnostic'] == '' and
            report['capturedFrames'] == report['writtenFrames'] == len(raw),
            'Owner incomplete or faulted')
    require(trace['test_only'] and trace['public_api_observer'] and
            trace['acquisition_version'] == 2 and len(trace['filters']) == 1,
            'Wrong acquisition observer')
    owner = trace['filters'][0]
    require(owner['ports'] == [{'index': 0, 'channel': 0, 'input': True}], 'Wrong route shape')
    for key in ('dropped', 'query_overflows', 'unknown_queries', 'unknown_events',
                'outer_allocations', 'outer_frees', 'outer_locks'):
        require(owner[key] == 0, 'Observer overflow/unknown/RT violation: ' + key)
    audit = report['audit']
    require(audit['allocations'] == audit['frees'] == audit['locks'] == 0 and
            audit['calls'] == owner['calls'] == len(owner['rows']), 'Audit/row scope mismatch')
    origin = report['timingOrigin']
    captured, declared_silence = 0, 0
    previous_ns = None
    ready_started = False
    for index, row in enumerate(owner['rows']):
        require(row['clock_known'] and row['id'] == origin['clockId'] and
                row['position'] == origin['graphPosition'] + captured and
                row['cycle'] == (origin['cycle'] + index) & 0xffffffff and
                row['rate_numerator'] == 1 and row['rate_denominator'] == 48000 and
                row['flags'] == 0 and row['calls'] == 1 and len(row['queries']) == 1,
                'Clock/route discontinuity')
        nsec = row['nsec']
        require(type(nsec) is int and 0 < nsec <= 0xffffffffffffffff and
                (previous_ns is None or nsec > previous_ns), 'Invalid/backward callback timestamp')
        expected_ns = origin['monotonicNs'] + captured * 1000000000 // 48000
        # Public integer frame positions and nanosecond phase can differ by one
        # sample at driver startup. Check the entire origin-relative span so
        # a small per-cycle error cannot accumulate without detection.
        require(abs(nsec - expected_ns) <= (1000000000 + 47999) // 48000,
                'Callback timestamp differs from device sample rate')
        previous_ns = nsec
        if index == 0:
            require(row['nsec'] == origin['monotonicNs'] and row['delay'] == origin['driverDelay'],
                    'Owner/observer origins differ')
        frames = row['duration']
        require(0 < frames <= 2048 and captured < len(raw), 'Unqualified callback extent')
        count = min(frames, len(raw) - captured)
        query = row['queries'][0]
        require(query['port'] == 0 and query['frames'] == frames and query['io_known'] and
                query['io_status'] == 2 and query['native_known'] and query['native_returned'] and
                query['sdk_dequeues'] == query['sdk_queues'] == 1 and query['queue_matched'] and
                query['queue_result'] >= 0 and not query['api_suppressed'] and
                query['chunk_bytes'] >= frames * 4 and
                query['chunk_offset'] + query['chunk_bytes'] <= query['extent_bytes'] and
                query['chunk_stride'] in (0, 4), 'Uncertified acquisition/lease')
        status = query['acquisition_status']
        if status == 3:
            require(not ready_started and query['chunk_flags'] == 2 and not query['returned'] and
                    not any(raw[captured:captured + count]), 'Invalid declared-silence prefix')
            declared_silence += count
        else:
            require(status == 1 and query['chunk_flags'] == 0 and query['returned'] and
                    query['known_buffer'], 'Unavailable/invalid data or silence after source start')
            ready_started = True
        captured += count
    require(captured == len(raw) and ready_started and
            declared_silence == waveform['zero_prefix_frames'], 'Prefix/trace lengths disagree')
    return {'all_callback_clocks_continuous': True, 'all_leases_returned_once': True,
            'declared_silence_frames': declared_silence, 'RT_audit_violations': 0}


def verify(workspace):
    workspace = Path(workspace)
    project = workspace / 'project-Δοκιμή'
    report = json.loads((project / 'probe.json').read_text())
    path = Path(report['assetPath'])
    require(not path.is_absolute() and '..' not in path.parts, 'Unsafe asset path')
    take = project / path
    require(hashlib.sha256(take.read_bytes()).hexdigest() == report['assetSha256'],
            'Original take hash differs')
    raw, source = float_wav(take), source_wav(workspace / 'source.wav')
    waveform = compare_waveforms(raw, source)
    clock = check_trace(report, json.loads((project / 'handoffs.json').read_text()), raw, waveform)
    return {'format': 'sc-input-acquisition-verification', 'schemaMajor': 1,
            'frames': len(raw), 'sample_rate': 48000, **waveform, **clock,
            'physical_audio_qualified': False, 'native_Windows_qualified': False,
            'startup_alignment_corrected': False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('workspace', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.workspace), indent=2))
