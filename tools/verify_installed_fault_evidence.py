#!/usr/bin/env python3
"""Replay retained action/route checks without audio, GUI or private host dumps."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / 'tests/results/X007'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def capsule(receipt):
    path = ROOT / receipt['archive']['path']
    assert path.resolve().is_relative_to(RESULTS.resolve())
    assert path.stat().st_size == receipt['archive']['bytes']
    assert digest(path.read_bytes()) == receipt['archive']['sha256']
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None
        manifest = json.loads(archive.read('manifest.json'))
        assert len(manifest) == receipt['archive']['logical_entries']
        result = {}
        for name, fingerprint in manifest.items():
            assert len(fingerprint) == 64 and all(c in '0123456789abcdef' for c in fingerprint)
            info = archive.getinfo('blobs/' + fingerprint)
            assert info.file_size <= 8 * 1024 * 1024
            data = archive.read(info)
            assert digest(data) == fingerprint
            result[name] = data
        return result


def main():
    original_path = RESULTS / '2026-10-08-installed-recording-fault.json'
    original = json.loads(original_path.read_text())
    supplement = json.loads((RESULTS / '2026-10-08-installed-recording-fault-supplement.json').read_text())
    assert digest(original_path.read_bytes()) == supplement['original_receipt_sha256']
    assert original['archive']['sha256'] == supplement['original_archive_sha256']
    capsule(original)
    files = capsule(supplement)
    action_names = sorted(n for n in files if n.startswith('actions/'))
    assert action_names == supplement['action_names'] and len(action_names) == 5
    timestamps = []
    for name in action_names:
        actions = json.loads(files[name])
        assert len(actions) == 1
        action = actions[0]
        command = action['command']
        assert command[:2] == ['/bin/bash', '-lc'] and len(command) == 3
        assert 'nsenter --target 3468392' in command[2]
        assert name.removeprefix('actions/').removesuffix('.json') in command[2]
        assert 'xdotool' in command[2]
        assert action['exit_code'] == (137 if name.endswith('save-quit.log.json') else 0)
        timestamps.append((action['timestamp'], name))
    assert len(set(t for t, _ in timestamps)) == 5
    assert [n for _, n in sorted(timestamps)] == [
        'actions/open-transport.log.json', 'actions/prepare-and-scroll.log.json',
        'actions/open-input.log.json', 'actions/record-and-read-fault.log.json',
        'actions/save-quit.log.json']
    before = json.loads(files['route/host-route-before-redacted.json'])
    after = json.loads(files['route/host-route-after-redacted.json'])
    assert before == after == supplement['host_route_inputs']['before']
    assert after == supplement['host_route_inputs']['after']
    for category, count in [('defaults', 'default_object_count'), ('links', 'link_object_count')]:
        assert len(before[category]) == before[count]
        for item in before[category]:
            fingerprint = item['canonical_object_sha256']
            assert len(fingerprint) == 64 and all(c in '0123456789abcdef' for c in fingerprint)
    print(json.dumps({'actions': 5, 'redacted_route_fingerprints_equal': True,
                      'capsule_integrity_verified': True, 'native_or_GUI_replay': False}))


if __name__ == '__main__':
    main()
