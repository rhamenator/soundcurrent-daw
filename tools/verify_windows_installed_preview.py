#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained installed-preview evidence; never starts Windows or audio."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from verify_windows_desktop import verify


def require(value, message):
    if not value:
        raise ValueError(message)


def verify_source_inputs(head, manifest):
    require(re.fullmatch(r'[0-9a-f]{40}', head), 'Invalid frozen source commit')
    # Historical capsules must remain verifiable after later implementation
    # changes. Read their exact Git commit, never silently substitute HEAD.
    result = subprocess.run(['git', 'cat-file', '-t', head], cwd=ROOT,
                            capture_output=True, text=True)
    require(result.returncode == 0 and result.stdout.strip() == 'commit',
            'Frozen source commit unavailable; fetch the recorded commit first')
    seen = set()
    require(0 < len(manifest['files']) <= 4096, 'Invalid native input manifest bounds')
    for row in manifest['files']:
        name = row['name']
        p = PurePosixPath(name)
        require(not p.is_absolute() and '..' not in p.parts and '\\' not in name and
                ':' not in name and p.as_posix() == name and name not in seen,
                'Invalid native source input path')
        seen.add(name)
        source = subprocess.run(['git', 'show', head + ':' + name], cwd=ROOT,
                                capture_output=True)
        require(source.returncode == 0 and
                hashlib.sha256(source.stdout).hexdigest() == row['sha256'],
                'Native build input differs from frozen Git source: ' + name)


def verify_native_modules(native, prepared, tools):
    prefix = native['installLocation'].casefold() + '\\'
    modules = {m['name'].casefold(): m['path'] for m in native['fixtureModules']}
    require(len(modules) == len(native['fixtureModules']), 'Duplicate native module name')
    for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll'):
        require(modules.get(name.casefold(), '').casefold().startswith(prefix),
                'Native fixture used non-installed dependency: ' + name)
    pinned = {row['name']: row for row in tools}
    require(native['fixtureSha256'] == pinned['sc-wasapi-ui-fixture.exe']['sha256'] and
            modules['sc-wasapi-ui-fixture.exe'].casefold() == native['fixtureExe'].casefold(),
            'Native fixture executable differs from qualified test tool')
    # qWait is an inline QtCore helper. MSVC discarded the unused QtTest
    # import library for this fixture; do not invent a loaded QtTest module.
    require(native['qtTestLoaded'] == ('qt6test.dll' in modules),
            'Native QtTest module presence differs')
    if 'qt6test.dll' in modules:
        require(modules['qt6test.dll'].casefold() ==
                (native['testOnlyDirectory'] + '\\Qt6Test.dll').casefold(),
                'Native fixture used unexpected QtTest location')
    require('Qt6Test.dll' not in prepared['payload'], 'Test DLL shipped as product payload')


def verify_observer_failure(installed, fault, journal, raw_journal, stderr, claims):
    require(installed['nativeWorkflowExit'] == 1 and
            installed['error'] == 'Installed-runtime native workflow failed' and
            stderr.strip() == 'Native playback observer fault',
            'Initial observer failure was lost')
    f = fault['fault']
    expected = f['previous']['position'] + f['previous']['duration']
    require(fault['format'] == 'soundcurrent-recording-fault' and
            f['rejected']['discontinuity'] and expected == claims['expectedNextPosition'] == 288000 and
            f['rejected']['position'] == claims['rejectedPosition'] == 288480 and
            f['capturedFrames'] == journal['committedFrames'] == claims['observerCommittedFrames'] == 287520 and
            raw_journal['committedFrames'] == claims['rawCommittedFrames'] == 480000,
            'Retained observer discontinuity details differ')


def run(receipt_path=None):
    receipt = json.loads((receipt_path or ROOT / 'tests/results/X007/2026-10-08-windows-installed-preview.json').read_text())
    inventory = receipt['capsule']
    archive = ((receipt_path.parent if receipt_path else ROOT / 'tests/results/X007') /
               inventory['name'])
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
        verify_source_inputs(receipt['sourceHead'], read('prepared/native-inputs.json'))
        audit = read('prepared/merged-native-input-audit.json')
        require(audit['sourceHead'] == receipt['sourceHead'] and
                audit['mismatches'] == ['tests/translation_catalog_tests.py'],
                'Conservative source audit exclusions differ')
        audited_files = read('prepared/native-source-audited-files.json')
        require(audited_files['sourceHead'] == receipt['sourceHead'] and
                audited_files['files'] == [r for r in audit['combinedFiles']
                                          if r['name'] not in audit['mismatches']] and
                any(r['name'] == 'tests/wasapi_ui_fixture.cpp' for r in audited_files['files']),
                'Native fixture source not retained')
        verify_source_inputs(receipt['sourceHead'], audited_files)
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
        proof = read('installed-native-module-proof/result.json')
        require(proof['sourceHead'] == receipt['sourceHead'] and proof['nativeExitCode'] == proof['exitCode'] == 0 and
                proof['installedExeSha256'] == expected_exe and proof['payloadVerified'] and
                proof['nativeAudioReplayed'] and proof['sessionId'] == 1 and
                not proof['compilerOnPath'] and not proof['qtSdkOnPath'],
                'Installed native module-proof workflow not accepted')
        verify_native_modules(proof, prepared, read('prepared/test-only-tools.json'))
        failure_prefix = 'initial-installed-observer-failure/Installed workflow - Ελληνικά/'
        fault_names = [n for n in inventory['files'] if n.startswith(failure_prefix + 'loopback/') and n.endswith('/first-fault.json')]
        raw_names = [n for n in inventory['files'] if n.startswith(failure_prefix + 'media/') and n.endswith('/journal.json')]
        require(len(fault_names) == len(raw_names) == 1, 'Ambiguous retained native failure media')
        verify_observer_failure(installed, read(fault_names[0]),
                                read(str(PurePosixPath(fault_names[0]).parent / 'journal.json')),
                                read(raw_names[0]),
                                z.read('initial-installed-observer-failure/native-workflow.stderr').decode('utf-8-sig'),
                                receipt['initialFailure'])
        project = Path(tmp) / 'unchanged-installed-native-pass/Installed workflow v3 - Ελληνικά'
        samples = verify(project)
        require(samples == read('verification/independent-native-v3.json') == receipt['independentMedia'],
                'Independent installed media result differs')
        proof_samples = verify(Path(tmp) / 'installed-native-module-proof/Installed workflow v6 - Ελληνικά')
        require(proof_samples == read('verification/independent-native-v6.json'),
                'Installed module-proof native media differs')
    print(json.dumps({'installedPreviewEvidenceVerified': True,
                      'nativeAudioReplayed': False, 'frames': samples['frames'],
                      'rawMaximumError': samples['rawMaximumError'],
                      'exportMaximumError': samples['exportMaximumError'],
                      'nativePlaybackMaximumResidual': samples['nativePlaybackMaximumResidual'],
                      'moduleProofPlaybackMaximumResidual': proof_samples['nativePlaybackMaximumResidual']}))


if __name__ == '__main__':
    run()
