#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify relocated, pinned original repeated recording evidence without audio replay."""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
from unittest.mock import patch
import zipfile
import verify_repeated_manual_stages as stages
import repeated_manual_stage_verifier_tests as mutations

RETAINED_ARCHIVE_SHA256 = 'e767ced8c635eeb41b8d50938124a869c4d937efc97cb9274ea9d1e97904f0bf'


def run(archive):
    data = archive.read_bytes()
    assert hashlib.sha256(data).hexdigest() == RETAINED_ARCHIVE_SHA256, 'Retained ZIP replaced'
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        assert len(names) == len(set(names)) and z.testzip() is None
        manifest = json.loads(z.read('manifest.json'))
        assert set(names) == set(manifest['payload_files']) | {'manifest.json'}
        for name, sha in manifest['payload_files'].items():
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            assert hashlib.sha256(z.read(name)).hexdigest() == sha, name
    with tempfile.TemporaryDirectory(prefix='sc-recording-feedback-') as temp:
        root = Path(temp).resolve()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(root)
        receipt = root / 'original-native/original-receipt.json'
        original = receipt.read_bytes()
        r = json.loads(original)
        project = root / 'original-native/original-run' / Path(r['project_directory']).name
        assert project.is_dir()
        reads, blocked = (Path.read_text, Path.read_bytes), []
        def guarded(method):
            def read(path, *args, **kwargs):
                if not path.absolute().is_relative_to(root):
                    blocked.append(str(path))
                    raise FileNotFoundError('Original-machine path unavailable')
                return method(path, *args, **kwargs)
            return read
        with patch.object(Path, 'read_text', guarded(reads[0])), \
                patch.object(Path, 'read_bytes', guarded(reads[1])):
            for name, module in [('stages', stages), ('mutations', mutations)]:
                output = root / (name + '.json')
                argv = [module.__file__, '--receipt', str(receipt), '--output', str(output)]
                with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                    try:
                        module.main()
                    except FileNotFoundError:
                        pass
                    else:
                        raise AssertionError('Original path remained usable')
                assert not output.exists()
                argv += ['--project', str(project)]
                with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                    module.main()
                bad = root / ('invalid-' + name + '.json')
                argv = [module.__file__, '--receipt', str(receipt), '--output', str(bad),
                        '--project', str(root / 'missing-project')]
                with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                    try:
                        module.main()
                    except OSError:
                        pass
                    else:
                        raise AssertionError('Invalid explicit project fell back')
                assert not bad.exists()
            # Bind every persisted raw asset to its original child oracle hash.
            session = json.loads((project / 'project.json').read_text())
            assets = {a['sha256']: a for a in session['assets']}
            bound = 0
            for row in r['child_result']['raw_lanes']:
                a = assets[row['sha256']]
                assert a['frames'] == row['frames'] and a['layout']['channels'] == 1
                assert hashlib.sha256((project / a['path']).read_bytes()).hexdigest() == row['sha256']
                bound += 1
            assert bound == 96
        assert receipt.read_bytes() == original
        result = json.loads((root / 'stages.json').read_text())
        refused = json.loads((root / 'mutations.json').read_text())['mutations_refused']
        assert result['bounded_repeated_native_workflow_qualified'] and len(refused) == 22
        return {'archive_integrity_verified': True, 'payload_files': len(manifest['payload_files']),
                'relocated_full_repeated_workflow_qualified': True,
                'mutations_refused': len(refused), 'persisted_raw_assets_bound_to_oracle': bound,
                'original_machine_path_reads_blocked': len(blocked),
                'invalid_explicit_projects_refused': 2, 'original_receipt_unchanged': True,
                'native_audio_replayed': False, 'historical48_cpu_cause_established': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, default=Path(__file__).resolve().parent /
                   'results/M2/native-manual-observations/2026-10-07-recording-monitor-feedback-evidence.zip')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = run(args.archive)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
