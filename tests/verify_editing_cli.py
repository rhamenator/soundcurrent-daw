#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned, inaudible CLI recording/editing/persistence acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--project-tool', required=True, type=Path)
parser.add_argument('--record-tool', required=True, type=Path)
args = parser.parse_args()
project_tool = args.project_tool.resolve()
record_tool = args.record_tool.resolve()
checks = 0


def check(value, reason):
    global checks
    checks += 1
    if not value:
        raise AssertionError(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory(prefix='sc-editing-cli-') as tmp:
    root = Path(tmp) / 'Séance — Українська'
    subprocess.run([str(record_tool), 'synthetic', str(root)], check=True,
                   capture_output=True, text=True, timeout=30)

    def run(command, *arguments):
        result = subprocess.run([str(project_tool), command, str(root), *map(str, arguments)],
                                capture_output=True, text=True, timeout=30)
        check(result.returncode == 0, result.stderr)
        return json.loads(result.stdout)

    def rejected(command, *arguments):
        before = digest(root / 'project.json')
        result = subprocess.run([str(project_tool), command, str(root), *map(str, arguments)],
                                capture_output=True, text=True, timeout=30)
        check(result.returncode != 0, 'Invalid command accepted')
        check(digest(root / 'project.json') == before, 'Failed command changed project')

    initial = run('inspect')
    first = initial['tracks'][0]['id']
    original_clip = initial['tracks'][0]['clips'][0]['id']
    asset = initial['assets'][0]
    raw = root / asset['path']
    raw_hash = digest(raw)
    added = run('add-track', '第二 — Ελλάδα', 'mono', first)
    second = added['tracks'][0]['id']
    check(second != first and len(added['tracks']) == 2, 'Insert identities/order wrong')
    check(added['tracks'][0]['name'] == '第二 — Ελλάδα', 'Unicode name changed')
    run('rename-track', first, 'Voice — Łódź')
    moved = run('move-track', first, second)
    check(moved['tracks'][0]['id'] == first, 'Track move order differs')
    project = root / 'project.json'
    previous = root / 'project.previous.json'
    before = (digest(project), project.stat().st_mtime_ns,
              digest(previous) if previous.exists() else None)
    run('move-track', first, first)
    check(before == (digest(project), project.stat().st_mtime_ns,
                     digest(previous) if previous.exists() else None), 'No-op rewrote files')
    split = run('split-clip', first, original_clip, 12345)
    clips = split['tracks'][0]['clips']
    right = clips[1]['id']
    check(clips[0]['id'] == original_clip and right != original_clip,
          'Split identity not preserved/fresh')
    check(clips[0]['lengthFrames'] == 12345 and clips[1]['sourceFrame'] == 12345,
          'Split extent differs')
    trimmed = run('trim-clip', first, right, 14000, 13000, 1000)
    check(trimmed['tracks'][0]['clips'][1]['lengthFrames'] == 1000, 'Trim not persisted')
    moved = run('move-clip', first, second, right, 20000)
    check(moved['tracks'][1]['clips'][0]['id'] == right and
          moved['tracks'][1]['clips'][0]['startFrame'] == 20000, 'Cross-track move differs')
    inserted = run('add-clip', second, asset['id'], 25000, 30000, 500)
    extra = inserted['tracks'][1]['clips'][1]['id']
    check(extra != right and inserted['assets'] == initial['assets'], 'Insert altered raw assets')
    rejected('trim-clip', second, extra, '-1', 0, 500)
    rejected('trim-clip', second, extra, '1.5', 0, 500)
    rejected('trim-clip', second, extra, '9223372036854775808', 0, 500)
    rejected('trim-clip', second, extra, 0, asset['frames'], 1)
    rejected('split-clip', second, right, 20000)
    rejected('move-clip', second, first, extra, '-1')
    rejected('add-track', 'Bad channels', 'discrete:257')
    rejected('add-track', 'Bad layout', 'banana')
    rejected('remove-track', '00000000-0000-0000-0000-000000000000')
    removed = run('remove-clip', second, extra)
    check(len(removed['tracks'][1]['clips']) == 1, 'Remove clip failed')
    final = run('remove-track', second)
    check(len(final['tracks']) == 1 and final['assets'] == initial['assets'],
          'Track removal deleted media metadata')
    check(digest(raw) == raw_hash, 'Editing changed raw recording')
    check(run('inspect') == final, 'Fresh process reopen differs')
    print(json.dumps({'checks': checks, 'owned_synthetic_frames': asset['frames'],
                      'raw_sha256_preserved': raw_hash, 'native_audio_actions': False,
                      'unicode_path_names': True, 'failed_commands_preserve_project': True,
                      'no_op_preserves_project_previous_and_mtime': True}))
