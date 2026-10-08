#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained installed-preview evidence; never starts Windows or audio."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from verify_windows_desktop import verify


def require(value, message):
    if not value:
        raise ValueError(message)


def run():
    receipt = json.loads((ROOT / 'tests/results/X007/2026-10-08-windows-installed-preview.json').read_text())
    inventory = receipt['capsule']
    archive = ROOT / 'tests/results/X007' / inventory['name']
    require(archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'],
            'Installed-preview capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-installed-win-') as tmp:
        require(len(z.infolist()) == inventory['payloads'] == len(inventory['files']) and
                {m.filename for m in z.infolist()} == set(inventory['files']),
                'Installed-preview capsule membership differs')
        require(sum(m.file_size for m in z.infolist()) <= 80 * 1024 * 1024,
                'Installed-preview capsule exceeds bounds')
        for m in z.infolist():
            p = PurePosixPath(m.filename)
            require(not p.is_absolute() and '..' not in p.parts and ':' not in m.filename and
                    '\\' not in m.filename and p.suffix.lower() not in {'.exe', '.dll', '.iso', '.ps1', '.cmd'},
                    'Unsafe installed-preview capsule entry')
            row = inventory['files'][m.filename]
            require(m.file_size == row['bytes'], 'Installed-preview payload size differs')
            b = z.read(m)
            require(hashlib.sha256(b).hexdigest() == row['sha256'], 'Installed-preview payload hash differs')
            dest = Path(tmp).joinpath(*p.parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(b)
        def read(name):
            return json.loads(z.read(name).decode('utf-8-sig'))
        prepared = read('prepared/receipt.json')
        installed = read('initial-installed-observer-failure/result.json')
        native = read('unchanged-installed-native-pass/result.json')
        require(prepared['sourceHead'] == installed['sourceHead'] == native['sourceHead'] == receipt['sourceHead'],
                'Installed-preview source identities differ')
        setup = [v for n, v in prepared['artifacts'].items() if n.endswith('-setup.exe')]
        require(len(setup) == 1 and setup[0]['sha256'] == installed['installerSha256'] == receipt['installerSha256'],
                'Installed setup identity differs')
        require(all(installed[k] == 0 for k in ('installExit', 'uninstallExit', 'reinstallExit')) and
                installed['payloadVerified'] and installed['projectAndSettingsPreserved'],
                'Normal installation/removal preservation not accepted')
        require(installed['runtimeBefore']['view64'].get('Installed') != 1 and
                installed['runtimeBefore']['view32'].get('Installed') != 1 and
                installed['runtimeAfter']['view64']['Installed'] == 1 and
                installed['runtimeAfter']['view32']['Installed'] == 1,
                'Clean runtime bootstrap evidence differs')
        require(all(not r['compilerOnPath'] and not r['qtSdkOnPath'] and r['sessionId'] == 1
                    for r in (installed, native)), 'Developer PATH or wrong native session')
        expected_exe = prepared['payload']['soundcurrent-daw.exe']['sha256']
        for name in ('main', 'reinstalledMain'):
            main = installed[name]
            require(main['exitCode'] == 0 and main['exeSha256'] == expected_exe,
                    'Actual installed main launch/normal-close identity differs')
            modules = {m['name'].casefold(): m['path'] for m in main['modules']}
            prefix = installed['installLocation'].casefold() + '\\'
            require(all(modules[n.casefold()].casefold().startswith(prefix)
                        for n in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll')),
                    'Installed main used nonlocal Qt/media/platform dependency')
        require(native['exitCode'] == native['nativeExitCode'] == 0 and native['payloadVerified'] and
                native['nativeAudioReplayed'] and native['installedExeSha256'] == expected_exe,
                'Unchanged installed native workflow not accepted')
        require(installed['nativeWorkflowExit'] == 1 and
                installed['error'] == 'Installed-runtime native workflow failed',
                'Initial observer failure was lost')
        project = Path(tmp) / 'unchanged-installed-native-pass/Installed workflow v3 - Ελληνικά'
        samples = verify(project)
        require(samples == read('verification/independent-native-v3.json') == receipt['independentMedia'],
                'Independent installed media result differs')
    print(json.dumps({'installedPreviewEvidenceVerified': True,
                      'nativeAudioReplayed': False, 'frames': samples['frames'],
                      'rawMaximumError': samples['rawMaximumError'],
                      'exportMaximumError': samples['exportMaximumError'],
                      'nativePlaybackMaximumResidual': samples['nativePlaybackMaximumResidual']}))


if __name__ == '__main__':
    run()
