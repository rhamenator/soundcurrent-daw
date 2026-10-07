#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify archived startup evidence with original machine paths unavailable."""
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
import native_startup_verifier_tests as mutations
import verify_native_startup_pair as pair


# Frozen external receipt: results/M2/2026-10-07-controlled-native-startup.json.
# Expected integrity must not come from the same archive being checked.
RETAINED_ARCHIVE_SHA256 = '5c72c7926e37ad006f8d954cac8214f58f9f6bae0682d4c1bb79826076004195'


def verify_archive_bytes(data):
    assert hashlib.sha256(data).hexdigest() == RETAINED_ARCHIVE_SHA256, 'Retained ZIP replaced'
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = z.namelist()
        assert len(names) == len(set(names)), 'Duplicate archive member'
        assert z.testzip() is None
        manifest = json.loads(z.read('manifest.json'))
        assert set(names) == set(manifest['payload_files']) | {'manifest.json'}, 'Unexpected members'
        for name, sha in manifest['payload_files'].items():
            assert not Path(name).is_absolute() and '..' not in Path(name).parts
            assert hashlib.sha256(z.read(name)).hexdigest() == sha, name
        return manifest


def run(archive):
    # Verify and extract the same immutable bytes, avoiding a second path read.
    data = archive.read_bytes()
    verify_archive_bytes(data)
    with tempfile.TemporaryDirectory(prefix='sc-startup-relocated-') as temp:
        root = Path(temp).resolve()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(root)
        receipt = root / 'joined-checks/native-startup-defer-native.json'
        original_bytes = receipt.read_bytes()
        recorded = json.loads(original_bytes)
        counterfactual = root / 'counterfactual' / Path(recorded['project_directory']).name
        assert counterfactual.is_dir()
        original = root / 'original47/original-project'
        raw = root / 'original47/independent-original-full-raw-analysis.json'
        blocked_reads = []
        read_text, read_bytes = Path.read_text, Path.read_bytes

        def guarded(method):
            def read(path, *args, **kwargs):
                if not path.absolute().is_relative_to(root):
                    blocked_reads.append(str(path))
                    raise FileNotFoundError('Original-machine path unavailable: ' + str(path))
                return method(path, *args, **kwargs)
            return read

        outputs = {}
        with patch.object(Path, 'read_text', guarded(read_text)), \
                patch.object(Path, 'read_bytes', guarded(read_bytes)):
            for name, module in [('pair', pair), ('mutations', mutations)]:
                output = root / (name + '-output.json')
                argv = [module.__file__, '--original', str(original),
                        '--counterfactual-receipt', str(receipt),
                        '--original-raw-analysis', str(raw), '--output', str(output)]
                # The archived absolute receipt path is genuinely inaccessible to
                # these verifier reads; no original directories are renamed/deleted.
                with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                    try:
                        module.main()
                    except FileNotFoundError:
                        pass
                    else:
                        raise AssertionError('Original temporary path was silently usable')
                assert not output.exists()
                reads_before = len(blocked_reads)
                relocated = argv + ['--counterfactual-project', str(counterfactual)]
                with patch.object(sys, 'argv', relocated), contextlib.redirect_stdout(io.StringIO()):
                    module.main()
                assert len(blocked_reads) == reads_before
                outputs[name] = json.loads(output.read_text())
                output.unlink()
                # An explicitly wrong relocation must fail, never fall back to
                # whichever historical directory happens to remain on this host.
                missing = argv + ['--counterfactual-project', str(root / 'missing-project')]
                with patch.object(sys, 'argv', missing), contextlib.redirect_stdout(io.StringIO()):
                    try:
                        module.main()
                    except FileNotFoundError:
                        pass
                    else:
                        raise AssertionError('Missing explicit project accepted')
                assert not output.exists() and len(blocked_reads) == reads_before
        assert receipt.read_bytes() == original_bytes
        assert outputs['pair']['controlled_startup_mechanism_qualified'] is True
        assert outputs['pair']['production_fix_implemented'] is False
        assert outputs['pair']['historical46_cause_established'] is False
        assert outputs['pair']['historical37_cause_established'] is False
        assert len(outputs['mutations']['mutations_refused']) == 21
        return {'relocated_pair_qualified': True, 'relocated_mutations_refused': 21,
                'original_machine_path_reads_blocked': len(blocked_reads),
                'missing_explicit_paths_refused': 2, 'original_receipt_unchanged': True,
                'archive_sha256': hashlib.sha256(data).hexdigest(),
                'native_audio_repeated': False, 'retained_runtime_observations': 47}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, default=Path(__file__).resolve().parent /
                   'results/M2/native-manual-observations/2026-10-07-controlled-startup-evidence.zip')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = run(args.archive)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
