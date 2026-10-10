#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Corrupt retained restart proof; never execute an installer or native endpoint."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
original = root / 'tests/results/M2/2026-10-10-installed-render-restart'
verifier = root / 'tools/verify_installed_render_recovery.py'
cases = ['positive', 'bad_archive_hash', 'wrong_scope', 'undo_changed', 'export_changed',
         'raw_changed', 'manifest_changed', 'binding_changed', 'helper_changed',
         'duplicate_process', 'failed_quit', 'extra_audio_node', 'package_helper_changed',
         'source_changed', 'ui_capture_binding_changed', 'native_helper_changed',
         'applied_asset_id_changed', 'applied_asset_path_changed', 'applied_asset_format_changed',
         'unrelated_applied_edit', 'worker_qualification_changed', 'protected_qualification_changed']
sha = lambda b: hashlib.sha256(b).hexdigest()
with tempfile.TemporaryDirectory(prefix='sc-render-restart-proof-') as td:
    out = Path(td)
    shutil.copyfile(original / 'source-archive-digest.json', out / 'source-archive-digest.json')
    for case in cases:
        receipt = json.loads((original / 'qualification.json').read_bytes())
        with zipfile.ZipFile(original / 'capture.zip') as z:
            entries = {n: z.read(n) for n in z.namelist()}
        def change(name, mutate):
            value = json.loads(entries[name])
            mutate(value)
            entries[name] = json.dumps(value, ensure_ascii=False).encode()
        if case == 'wrong_scope':
            receipt['windowsInstalledRestartQualified'] = True
        if case == 'undo_changed':
            change('workspace/undone-project.json', lambda j: j.update(name='Unexpected edit'))
        if case in ['export_changed', 'raw_changed']:
            n = ('workspace/reopened-mix.wav' if case == 'export_changed' else
                 'workspace/project-été-Κиїв/media/raw-été.wav')
            b = bytearray(entries[n])
            b[-4] ^= 1  # Guarantee a changed sample even when its tail was silent.
            entries[n] = bytes(b)
        if case == 'manifest_changed':
            n = next(n for n in entries if '/stretch-selections/' in n and n.endswith('.json'))
            change(n, lambda j: j['request'].update(timeNumerator=499999, timeDenominator=333333))
        if case == 'binding_changed':
            n = next(n for n in entries if n.endswith('/selection.sha256'))
            entries[n] = b'0' * 64
        if case == 'helper_changed':
            entries['workspace/helper-executable.txt'] = b'/tmp/unqualified-helper\n'
        if case == 'duplicate_process':
            entries['workspace/app-pid-2.txt'] = entries['workspace/app-pid-1.txt']
        if case == 'failed_quit':
            entries['workspace/exit-5.txt'] = b'1\n'
        if case == 'extra_audio_node':
            change('workspace/graph-after-2.json', lambda j: j.append({
                'type': 'PipeWire:Interface:Node', 'info': {'props': {'node.name': 'Unexpected'}}}))
        if case == 'package_helper_changed':
            change('package-receipt.json', lambda j: j['install_input_sha256'].update({
                'usr/bin/sc-stretch-render-worker': '0' * 64}))
        if case == 'source_changed':
            change('native-linux-qualification.json', lambda j: j.update(source_head='0' * 40))
        if case == 'native_helper_changed':
            change('native-linux-qualification.json', lambda j: j['executable_sha256'].update({
                'sc-stretch-render-worker': '0' * 64}))
            change('package-receipt.json', lambda j: j.update(qualification_sha256=sha(
                entries['native-linux-qualification.json'])))
        if case.startswith('applied_asset_') or case == 'unrelated_applied_edit':
            def corrupt_applied(j):
                if case == 'applied_asset_id_changed':
                    j['assets'][1]['id'] = '11111111-1111-4111-8111-111111111111'
                elif case == 'applied_asset_path_changed':
                    j['assets'][1]['path'] = 'media/unregistered.wav'
                elif case == 'applied_asset_format_changed':
                    j['assets'][1]['sampleRate'] = 44100
                else:
                    j['tracks'][0]['name'] = 'Unexpected unrelated edit'
            for n in ['applied-project.json', 'redone-project.json', 'before-reopen-project.json']:
                change('workspace/' + n, corrupt_applied)
        if case == 'worker_qualification_changed':
            change('native-v4.json', lambda j: j.update(exeSha256='0' * 64))
        if case == 'protected_qualification_changed':
            change('native-v5.json', lambda j: j.update(workerSha256='0' * 64))
        if case == 'ui_capture_binding_changed':
            entries['workspace/attached-selected.png'] += b'changed'
        with zipfile.ZipFile(out / 'capture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for n, b in entries.items():
                z.writestr(n, b)
        b = (out / 'capture.zip').read_bytes()
        # Rebuild outer integrity so structured failures reach semantic checks.
        receipt['archive'] = {'name': 'capture.zip', 'bytes': len(b), 'sha256': sha(b), 'entries': {
            n: {'bytes': len(b), 'sha256': sha(b)} for n, b in entries.items()}}
        if case == 'bad_archive_hash':
            receipt['archive']['sha256'] = '0' * 64
        (out / 'qualification.json').write_text(json.dumps(receipt))
        result = subprocess.run([sys.executable, str(verifier), '--receipt',
                                 str(out / 'qualification.json')], capture_output=True,
                                text=True, timeout=15)
        assert (result.returncode == 0) == (case == 'positive'), (case, result.stdout, result.stderr)
print(f'installed_render_recovery_evidence_checks={len(cases)} positive=true '
      'integrity_scope_state_media_identity_refusals=true native_replay=false')
