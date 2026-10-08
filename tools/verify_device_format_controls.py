#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check retained control evidence; never starts a VM or audio device."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs

ROOT = Path(__file__).resolve().parents[1]


def run():
    receipt = json.loads((ROOT / 'tests/results/X007/2026-10-08-device-format-diagnostics.json').read_text())
    inventory = receipt['capsule']
    archive = ROOT / 'tests/results/X007' / inventory['name']
    require(archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'],
            'Control capsule identity differs')
    with zipfile.ZipFile(archive) as z:
        require(len(z.infolist()) == len(inventory['files']) <= 64 and
                {m.filename for m in z.infolist()} == set(inventory['files']) and
                sum(m.file_size for m in z.infolist()) <= 16 * 1024 * 1024,
                'Control capsule membership or bounds differ')
        for m in z.infolist():
            p = PurePosixPath(m.filename)
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in m.filename and
                    ':' not in m.filename and p.suffix in {'.json', '.log', '.stdout', '.stderr'},
                    'Unsafe control evidence path')
            row = inventory['files'][m.filename]
            require(m.file_size == row['bytes'] and
                    hashlib.sha256(z.read(m)).hexdigest() == row['sha256'],
                    'Control payload identity differs')
        def read(name):
            return json.loads(z.read(name).decode('utf-8-sig'))
        source = read('native/inputs.json')
        require(len(source['files']) == receipt['sourceInputFiles'] == 336,
                'Native input count differs')
        verify_source_inputs(receipt['sourceHead'], source)
        native = read('native/results/result.json')
        require(native == receipt['windows'] and native['exitCode'] == 0 and
                native['sessionId'] == 1 and native['platform'] == 'windows',
                'Native control result differs')
        require({t['label'] for t in native['tests']} ==
                {'ui-focused', 'ui-full', 'localization', 'ports'} and
                len(native['tests']) == 4 and all(t['exitCode'] == 0 for t in native['tests']),
                'Native controls not accepted')
        tools = {t['label']: t for t in native['tests']}
        require(tools['ui-focused']['exeSha256'] == tools['ui-full']['exeSha256'],
                'Focused/full controls used different native executable')
        focused = read('native/results/ui-focused.stdout')
        require(focused['native_format_playback'] and focused['native_format_recording'] and
                focused['physical_devices'] is False, 'Wrong native format-test scope')
        full = read('native/results/ui-full.stdout')
        require(full['ui_keyboard_undo'] and full['playback_ui_fake_endpoint'] and
                full['close_waits_playback_join'], 'Surrounding native workflows not accepted')
        ports = z.read('native/results/ports.stdout').decode('utf-8-sig').strip()
        require(ports == '26 route admission checks passed', 'Native port admission result differs')
        require(b'No native audio; no language fully qualified.' in
                z.read('native/results/localization.stdout'), 'Localization scope differs')
        require(b'100% tests passed' in z.read('linux/corrected-ui-run.log') and
                b'100% tests passed' in z.read('linux/sanitizers.log') and
                b'FAIL: Timed out awaiting UI workflow at line 362' in
                z.read('linux/initial-targeted-run.log'), 'Linux acceptance/failure evidence lost')
        require(not receipt['nativeAudioReplayed'] and not receipt['physicalDevicesQualified'] and
                not receipt['installedPreviewsRebuilt'] and not receipt['parityPromoted'],
                'Control proof promoted beyond its scope')
    print(json.dumps({'retainedDeviceFormatControlsVerified': True,
                      'nativeAudioReplayed': False, 'physicalDevicesQualified': False}))


if __name__ == '__main__':
    run()
