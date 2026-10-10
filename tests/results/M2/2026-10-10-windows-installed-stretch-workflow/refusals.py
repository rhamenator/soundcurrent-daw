# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained-data refusals even with consistently recomputed ZIP manifests."""
from pathlib import Path
import hashlib, json, subprocess, sys, tempfile, zipfile

root = Path(__file__).resolve().parent
cases = ('positive', 'summary_quality', 'nested_audio', 'sdk_module', 'helper_identity',
         'source_anchor', 'action_parameters', 'undo_state', 'repeat_export', 'cycle_preservation', 'builder_scope')
with tempfile.TemporaryDirectory(prefix='sc-installed-stretch-proof-') as tmp:
    out = Path(tmp)
    for case in cases:
        r = json.loads((root / 'qualification.json').read_bytes())
        with zipfile.ZipFile(root / 'capture.zip') as z:
            data = {n: z.read(n) for n in z.namelist()}
        def change(name, edit):
            value = json.loads(data[name].decode('utf-8-sig'))
            edit(value)
            data[name] = json.dumps(value, ensure_ascii=False).encode()
        if case == 'summary_quality':
            r['fullProcessingQuality'] = True
        elif case == 'nested_audio':
            change('observations/actions.json', lambda v: v.update(nativeAudioActivated=True))
        elif case == 'sdk_module':
            def edit(v):
                next(m for m in v['modules'] if m['name'] == 'Qt6Core.dll')['path'] = 'C:\\SDK\\Qt6Core.dll'
            change('second/main-20261010004655681.json', edit)
        elif case == 'helper_identity':
            def edit(v):
                v['processes'][0]['sha256'] = '0' * 64
            change('first/helper-observer.json', edit)
        elif case == 'source_anchor':
            def edit(v):
                v['tracks'][0]['clips'][0]['stretch']['sourceOrigin']['fraction'] = 0
            # Change every copy consistently; equality alone must not qualify it.
            for n in data:
                if n.endswith(('project-été-Κиїв/project.json', 'applied-project.json',
                               'redone-project.json', 'before-reopen-project.json')):
                    change(n, edit)
        elif case == 'undo_state':
            change('first/undone-project.json', lambda v: v.update(name='Changed'))
        elif case == 'action_parameters':
            change('observations/actions.json', lambda v: v.update(
                selectedExactSourceOrigin={'frame': 19, 'fraction': 0, 'denominator': 1},
                sourceFrames=128, timeNumerator=2, timeDenominator=1,
                pitchMilliCents=-100000, formantPreserved=False))
        elif case == 'repeat_export':
            name = 'second/project-été-Κиїв-mix.wav'
            b = bytearray(data[name]); b[-1] ^= 1; data[name] = bytes(b)
        elif case == 'cycle_preservation':
            def edit(v):
                files = json.loads(v['dataAfter']); files[0]['sha256'] = '0' * 64
                v['dataBefore'] = v['dataAfter'] = json.dumps(files)
            change('cycle/cycle-result.json', edit)
        elif case == 'builder_scope':
            change('first/receipt.json', lambda v: v.update(cleanInstallQualified=True))
        with zipfile.ZipFile(out / 'capture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name, b in data.items():
                z.writestr(name, b)
        sha = lambda b: hashlib.sha256(b).hexdigest()
        b = (out / 'capture.zip').read_bytes()
        r['archive'] = {'bytes': len(b), 'sha256': sha(b), 'entries': {
            n: {'bytes': len(b), 'sha256': sha(b)} for n, b in data.items()}}
        (out / 'qualification.json').write_text(json.dumps(r))
        p = subprocess.run([sys.executable, str(root / 'verify.py'), '--receipt',
                            str(out / 'qualification.json')], capture_output=True, text=True, timeout=15)
        assert (p.returncode == 0) == (case == 'positive'), (case, p.stdout, p.stderr)
print('Retained installed stretch workflow: 11 positive/refusal cases pass; no guest or installer replay.')
