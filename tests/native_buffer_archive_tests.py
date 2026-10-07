#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify fixed original buffer-acquisition evidence after relocation, without audio."""
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
import native_buffer_startup_verifier_tests as mutations
import verify_native_buffer_startup as startup
from verify_native_port_handoff import analyze as handoff_analysis
from verify_pipewire_manual_priority import priority
from verify_pipewire_manual_trace import analyze as marker_analysis

RETAINED_ARCHIVE_SHA256 = '33bfa302bba8d5d3ec6f45e63c646194b2300e6ff10d628221e7bbb75cf6a0d4'


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
    data = archive.read_bytes()
    manifest = verify_archive_bytes(data)
    with tempfile.TemporaryDirectory(prefix='sc-native-buffer-relocated-') as temp:
        root = Path(temp).resolve()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            z.extractall(root)
        receipt = root / 'startup/receipt.json'
        original = receipt.read_bytes()
        read_text, read_bytes = Path.read_text, Path.read_bytes
        blocked = []
        def guarded(method):
            def read(path, *args, **kwargs):
                if not path.absolute().is_relative_to(root):
                    blocked.append(str(path))
                    raise FileNotFoundError('Original-machine path unavailable')
                return method(path, *args, **kwargs)
            return read
        with patch.object(Path, 'read_text', guarded(read_text)), \
                patch.object(Path, 'read_bytes', guarded(read_bytes)):
            # Prove the missing original path is actually refused, not available
            # incidentally on a developer's machine.
            try:
                startup.load(receipt)
            except FileNotFoundError:
                pass
            else:
                raise AssertionError('Original machine path remained usable')
            for name, module in [('startup', startup), ('mutations', mutations)]:
                output = root / (name + '-output.json')
                argv = [module.__file__, '--receipt', str(receipt), '--project',
                        str(root / 'startup/project'), '--output', str(output)]
                with patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                    module.main()
            for case in ['ordinary-stop', 'ordinary-cancel']:
                r = json.loads((root / case / 'receipt.json').read_text())
                priority(r['child_result'])
                p = root / case / 'project'
                assert not (p / 'native-startup-gate.json').exists()
                h = json.loads((p / 'native-port-handoff.json').read_text())
                m = {role: json.loads((p / (role + '-port-markers.json')).read_text())
                     for role in ['owner', 'source']}
                assert h['acquisition_version'] == 2
                handoff_analysis(h, m)
                assert marker_analysis(p)['qualified_trace'] is True
            for bad in [root / 'missing', root / 'startup/receipt.json']:
                try:
                    startup.load(receipt, bad)
                except (OSError, AssertionError):
                    pass
                else:
                    raise AssertionError('Invalid explicit path fell back')
        assert receipt.read_bytes() == original
        result = json.loads((root / 'startup-output.json').read_text())
        refused = json.loads((root / 'mutations-output.json').read_text())['mutations_refused']
        assert result['production_startup_readiness_qualified'] and len(refused) == 60
        # The failed run remains failed when its exact original audio is retained.
        failure = json.loads((root / 'original48/original-failure.json').read_text())
        assert failure['exit_code'] == 1 and 'error' in failure
        raw = json.loads((root / 'original48/independent-original-full-raw-analysis.json').read_text())
        prefix = json.loads((root / 'original48/independent-original-output-prefix-analysis.json').read_text())
        assert raw['full_raw_qualified'] and raw['raw_samples_verified'] == 1557696
        assert prefix['output_prefix_qualified'] and prefix['output_prefix_samples_verified'] == 452608
        assert not prefix['full_target_output_qualified'] and not prefix['runtime_timing_qualified']
        return {'original_archive_integrity_verified': True,
                'payload_files': len(manifest['payload_files']),
                'relocated_production_startup_qualified': True,
                'relocated_ordinary_stop_cancel_qualified': True,
                'mutations_refused': len(refused), 'invalid_explicit_paths_refused': 2,
                'original_machine_path_reads_blocked': len(blocked),
                'original_receipt_unchanged': True, 'original48_still_unqualified': True,
                'native_audio_replayed': False, 'retained_runtime_observations': 48}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, default=Path(__file__).resolve().parent /
                   'results/M2/native-manual-observations/2026-10-07-native-buffer-acquisition-evidence.zip')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    result = run(args.archive)
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
