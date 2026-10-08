#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded deployment-input refusal tests; no installer or audio activation."""
from copy import deepcopy
import hashlib
from pathlib import Path
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from package_windows_preview import deploy_payload, qualified_dependencies, qualify_worker
import json


def run():
    names = ('soundcurrent-daw.exe', 'sndfile.dll', 'Qt6Core.dll',
             'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll')
    data = {n: ('test-only:' + n).encode() for n in names}
    manifest = {'files': [{'path': n, 'bytes': len(b),
                          'sha256': hashlib.sha256(b).hexdigest()}
                         for n, b in data.items()]}
    mutations = {
        'traversal': '../bad.dll', 'absolute': '/bad.dll', 'drive': 'C:/bad.dll',
        'reserved-device': 'CON.dll', 'trailing-dot': 'bad.dll.', 'empty-component': '.',
    }
    cases = ['native-separators', *mutations, 'normalized-duplicate', 'missing-file',
             'hash', 'size', 'oversize', 'symlink', 'developer-crt', 'case-collision']
    with tempfile.TemporaryDirectory(prefix='sc-windows-package-') as tmp:
        root = Path(tmp)
        for case in cases:
            m = deepcopy(manifest)
            entries = [(n.replace('/', '\\'), b, False) for n, b in data.items()]
            if case in mutations:
                entries += [(mutations[case], b'bad', False)]
            elif case == 'normalized-duplicate':
                entries += [('platforms/qwindows.dll', data['platforms/qwindows.dll'], False)]
            elif case == 'missing-file':
                entries.pop()
            elif case == 'hash':
                m['files'][0]['sha256'] = '0' * 64
            elif case == 'size':
                m['files'][0]['bytes'] += 1
            elif case == 'oversize':
                m['files'][0]['bytes'] = 64 * 1024 * 1024 + 1
            elif case == 'symlink':
                entries[0] = (entries[0][0], entries[0][1], True)
            elif case == 'developer-crt':
                b = b'test-only'
                entries += [('vcruntime140.dll', b, False)]
                m['files'] += [{'path': 'vcruntime140.dll', 'bytes': len(b),
                               'sha256': hashlib.sha256(b).hexdigest()}]
            elif case == 'case-collision':
                m['files'] += [{**m['files'][2], 'path': 'qt6core.dll'}]
            archive = root / (case + '.zip')
            with zipfile.ZipFile(archive, 'w') as z:
                for n, b, symlink in entries:
                    info = zipfile.ZipInfo(n)
                    if symlink:
                        info.create_system = 3
                        info.external_attr = 0o120777 << 16
                    z.writestr(info, b)
            destination = root / case
            destination.mkdir()
            try:
                deploy_payload(archive, m, destination)
            except ValueError:
                if case == 'native-separators':
                    raise
            else:
                if case != 'native-separators':
                    raise AssertionError('Unsafe deployment accepted: ' + case)
                assert all((destination / n).read_bytes() == b for n, b in data.items())
                try:deploy_payload(archive,m,destination,require_worker=True)
                except ValueError:pass
                else:raise AssertionError('New desktop payload admitted missing worker')
    trusted = json.loads((Path(__file__).resolve().parents[1] /
                          'research/windows-preview-dependencies.json').read_text())
    pinned = {'files': list(trusted['files'].values())}
    qualified_dependencies(pinned)
    worker={'path':'sc-import-inspect-worker.exe','bytes':123,'sha256':'a'*64}
    with_worker={'files':pinned['files']+[worker]}
    qualified_dependencies(with_worker)
    head='b'*40
    receipt={'sourceCommit':head,'exitCode':0,'pid':1234,'exeSha256':'a'*64}
    qualify_worker(with_worker,receipt,head)
    for bad in (None,{**receipt,'sourceCommit':'c'*40},{**receipt,'exitCode':1},
                {**receipt,'pid':0},{**receipt,'exeSha256':'0'*64}):
        try:qualify_worker(with_worker,bad,head)
        except ValueError:pass
        else:raise AssertionError('Unqualified worker accepted')
    try:qualify_worker(pinned,receipt,head)
    except ValueError:pass
    else:raise AssertionError('Missing worker accepted')
    wrong = deepcopy(pinned)
    wrong['files'][0]['sha256'] = '0' * 64
    try:
        qualified_dependencies(wrong)
    except ValueError:
        pass
    else:
        raise AssertionError('Self-consistent wrong dependency identity accepted')
    print('Pinned native dependencies: accepted qualified identities; refused replacement DLL')
    print('Windows deployment inputs: accepted native separators; refused 14 mutations')


if __name__ == '__main__':
    run()
