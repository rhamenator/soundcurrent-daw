#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recover a native exhaustion fixture on an independent owned copy, retaining originals."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def snapshot(root):
    files = sorted(p for p in root.rglob('*') if p.is_file())
    assert all(not p.is_symlink() for p in root.rglob('*'))
    return {str(p.relative_to(root)): {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in files}


class Info(ctypes.Structure):
    _fields_ = [('frames', ctypes.c_int64)] + [(n, ctypes.c_int) for n in
               ('samplerate', 'channels', 'format', 'sections', 'seekable')]


def compare_audio(old, new, frames):
    lib = ctypes.CDLL('libsndfile.so.1')
    lib.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(Info)]
    lib.sf_open.restype = ctypes.c_void_p
    lib.sf_readf_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    lib.sf_readf_float.restype = ctypes.c_int64
    lib.sf_close.argtypes = [ctypes.c_void_p]
    lib.sf_close.restype = ctypes.c_int
    handles = []
    try:
        for path in (old, new):
            info = Info()
            handle = lib.sf_open(bytes(path), 0x10, ctypes.byref(info))
            assert handle, str(path)
            handles.append(handle)
            assert info.frames == frames and info.channels == 1 and info.samplerate == 48000
        buffers = [(ctypes.c_float * 4096)(), (ctypes.c_float * 4096)()]
        copied = 0
        while copied < frames:
            count = min(4096, frames - copied)
            for handle, buffer in zip(handles, buffers):
                assert lib.sf_readf_float(handle, buffer, count) == count
            assert ctypes.string_at(buffers[0], count * 4) == ctypes.string_at(buffers[1], count * 4)
            copied += count
        return copied
    finally:
        for handle in handles:
            assert lib.sf_close(handle) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probes', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    exhausted = next(r for r in json.loads(args.probes.read_text()) if r['mode'] == 'writer-stall')
    source = Path(exhausted['project_directory'])
    before = snapshot(source)
    initial = json.loads((source / 'project.json').read_text())
    destination = Path(tempfile.mkdtemp(prefix='sc-checkpoint-recovery-', dir=source.parent.parent)) / 'Reprise — Ελληνικά'
    shutil.copytree(source, destination)
    for name in before:
        assert (source / name).stat().st_ino != (destination / name).stat().st_ino
    receipts = []
    samples = 0
    for track in initial['tracks'][1:]:
        journals = [(p, json.loads(p.read_text())) for p in (destination / 'media').glob('capture-*/journal.json')]
        job, old = next((p.parent, j) for p, j in journals if j['trackId'] == track['id'] and not j['recoveredFrom'])
        result = subprocess.run([str(args.binary.resolve()), 'recover', str(destination), str(job)],
                                capture_output=True, text=True, check=True, timeout=60)
        current = json.loads((destination / 'project.json').read_text())
        new_track = next(t for t in current['tracks'] if t['id'] == track['id'])
        assert len(new_track['clips']) == 1
        clip = new_track['clips'][0]
        asset = next(a for a in current['assets'] if a['id'] == clip['assetId'])
        journal = json.loads((destination / Path(asset['path']).parent / 'journal.json').read_text())
        assert journal['recoveredFrom'] == old['assetId'] and journal['assetId'] != old['assetId']
        assert journal['timingOrigin'] == old['timingOrigin'] and journal['sampleSha256'] == old['sampleSha256']
        assert asset['frames'] == old['committedFrames'] and journal['endReason'] == 10
        assert clip['startFrame'] == max(0, old['startFrame'] - old['inputLatencyFrames'])
        assert clip['sourceFrame'] == max(0, old['inputLatencyFrames'] - old['startFrame'])
        assert clip['lengthFrames'] == asset['frames'] - clip['sourceFrame']
        assert sha(destination / asset['path']) == asset['sha256']
        samples += compare_audio(job / 'take.wav', destination / asset['path'], asset['frames'])
        receipts.append({'source_asset': old['assetId'], 'recovered_asset': asset['id'], 'frames': asset['frames'],
                         'timing_origin': journal['timingOrigin'], 'sample_sha256': journal['sampleSha256'],
                         'media_sha256': asset['sha256'], 'cli_stdout': result.stdout})
    current = json.loads((destination / 'project.json').read_text())
    assert len(current['assets']) == len(initial['assets']) + 32 and len(receipts) == 32
    assert snapshot(source) == before
    for name, item in before.items():
        if name not in ('project.json', 'project.previous.json'):
            assert sha(destination / name) == item['sha256']
    backup = json.loads((destination / 'project.previous.json').read_text())
    assert len(backup['assets']) == len(current['assets']) - 1
    assert samples == exhausted['verified_raw_samples']
    args.output.write_text(json.dumps({'source_project': str(source), 'independent_copy': str(destination),
        'originals_unchanged': True, 'copied_media_and_journals_unchanged': True, 'copy_project_backup_updated': True, 'original_files': before,
        'recovered_tracks': 32, 'exact_recovered_samples': samples, 'save_reopen': True,
        'native_callbacks_started': False, 'receipts': receipts}, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'recovered_tracks': 32, 'exact_samples': samples, 'originals_unchanged': True}))


if __name__ == '__main__':
    main()
