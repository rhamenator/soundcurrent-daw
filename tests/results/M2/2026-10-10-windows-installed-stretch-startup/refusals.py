# SPDX-License-Identifier: GPL-3.0-only
"""Exercise retained-observation gates, including rehashed false scope claims."""
from pathlib import Path
import hashlib, json, subprocess, sys, tempfile, zipfile

root = Path(__file__).resolve().parent
cases = ('positive', 'summary_scope', 'nested_scope', 'sdk_module', 'invented_exit',
         'package_scope', 'changed_project', 'changed_raw')
with tempfile.TemporaryDirectory(prefix='sc-installed-startup-proof-') as tmp:
    out = Path(tmp)
    for case in cases:
        r = json.loads((root / 'qualification.json').read_bytes())
        with zipfile.ZipFile(root / 'capture.zip') as z:
            data = {n: z.read(n) for n in z.namelist()}
        def change(name, edit):
            value = json.loads(data[name].decode('utf-8-sig'))
            edit(value)
            data[name] = json.dumps(value, ensure_ascii=False).encode()
        if case == 'summary_scope':
            r['fullStretchAcceptance'] = True
        if case == 'nested_scope':
            change('installed/installed-verification.json', lambda v: v.update(fullStretchAcceptance=True))
        if case == 'sdk_module':
            def edit(v):
                next(m for m in v['modules'] if m['name'] == 'Qt6Core.dll')['path'] = 'C:\\SDK\\Qt6Core.dll'
            change('installed/main-20261010002451001.json', edit)
        if case == 'invented_exit':
            # Consistently change both copies; the original observed scope must still fail.
            for name in ('first/setup-result.json', 'installed/setup-result.json'):
                change(name, lambda v: v.update(setupExit=2))
        if case == 'package_scope':
            for name in ('first/receipt.json', 'installed/receipt.json'):
                change(name, lambda v: v.update(cleanInstallQualified=True))
        if case == 'changed_project':
            change('installed/project-été-Κиїв/project.json', lambda v: v.update(name='Changed'))
        if case == 'changed_raw':
            name = 'installed/project-été-Κиїв/media/raw-été.wav'
            b = bytearray(data[name]); b[-1] ^= 1; data[name] = bytes(b)
        with zipfile.ZipFile(out / 'capture.zip', 'w', zipfile.ZIP_DEFLATED) as z:
            for name, b in data.items(): z.writestr(name, b)
        sha = lambda b: hashlib.sha256(b).hexdigest()
        b = (out / 'capture.zip').read_bytes()
        r['archive'] = {'bytes': len(b), 'sha256': sha(b), 'entries': {
            n: {'bytes': len(b), 'sha256': sha(b)} for n, b in data.items()}}
        (out / 'qualification.json').write_text(json.dumps(r))
        p = subprocess.run([sys.executable, str(root / 'verify.py'), '--receipt',
                            str(out / 'qualification.json')], capture_output=True, text=True, timeout=15)
        assert (p.returncode == 0) == (case == 'positive'), (case, p.stdout, p.stderr)
print('Retained installed startup: 8 positive/refusal cases pass; no guest or installer replay.')
