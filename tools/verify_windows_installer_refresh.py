#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify installed UI acceptance and retained capture failures; no audio replay."""
from array import array
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from verify_input_acquisition import float_wav


def modules(rows, location):
    names = {r['name'].casefold(): r['path'] for r in rows}
    require(len(names) == len(rows), 'Duplicate installed module name')
    for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll'):
        require(names.get(name.casefold(), '').casefold().startswith(location.casefold()+'\\'),
                'Non-installed dependency: '+name)


def execution(result, prepared, tools):
    require(result['sourceHead'] == prepared['sourceHead'] and result['sessionId'] == 1 and
            result['payloadVerified'] and not result['compilerOnPath'] and not result['qtSdkOnPath'],
            'Installed source/session/PATH/payload differs')
    modules(result['fixtureModules'], result['installLocation'])
    pinned = {r['name']: r for r in tools}
    require(result['fixtureSha256'] == pinned['sc-wasapi-ui-fixture.exe']['sha256'] and
            'Qt6Test.dll' not in prepared['payload'], 'Fixture identity or test-only scope differs')
    require(result['exitCode'] == result['nativeWorkflowExit'] == 1 and
            result['error'] == 'Installed-runtime native workflow failed',
            'Original native workflow failure lost')


def failure(root, expected):
    project = root/'Installed workflow - Ελληνικά'
    faults = list(project.rglob('first-fault.json'))
    require(len(faults) == 1, 'Ambiguous retained capture fault')
    p = faults[0]
    f = json.loads(p.read_text())['fault']
    j = json.loads((p.parent/'journal.json').read_text())
    previous, rejected = f['previous'], f['rejected']
    require(f['status'] == 6 and f['reason'] == 5 and rejected['discontinuity'] and
            rejected['position'] - previous['position'] - previous['duration'] == 480 and
            f['capturedFrames'] == j['committedFrames'] == expected['committedFrames'] and
            previous['position'] + previous['duration'] == expected['expectedPosition'] and
            rejected['position'] == expected['rejectedPosition'] and
            ('loopback' in p.parts) == expected['observer'] and
            (root/'native-workflow.stderr').read_text(encoding='utf-8-sig').strip() == expected['stderr'],
            'Retained discontinuity details differ')
    source = (project/'source-stereo.f32').read_bytes()
    # Exact deterministic bank from the independently checked native desktop
    # experiment; the source fixture is also pinned to Git below.
    require(hashlib.sha256(source).hexdigest() ==
            'c015ddfff9e89a24c1b71e244223aab751b6bf331389a4ebfdd03a82d7728293',
            'Deterministic fixture bank changed')
    source_samples = array('f'); source_samples.frombytes(source)
    if sys.byteorder != 'little': source_samples.byteswap()
    source_samples = source_samples[::2]
    raw_journals = list((project/'media').glob('*/journal.json'))
    require(len(raw_journals) == 1, 'Ambiguous raw recording journal')
    raw_journal = json.loads(raw_journals[0].read_text())
    raw = float_wav(raw_journals[0].parent/'take.wav')
    require(len(raw) == raw_journal['committedFrames'] == expected['rawFrames'] and
            raw_journal['phase'] == 'finalized', 'Raw extent/journal differs')
    marker = next((n for n,v in enumerate(raw) if abs(v) > .0001), len(raw))
    require(marker < len(raw), 'Missing raw source marker')
    needle = raw[marker:marker+32]
    offsets = [n-marker for n in range(12000,96000)
               if source_samples[n:n+32] == needle and n >= marker]
    require(len(offsets) == 1 and
            raw == source_samples[offsets[0]:offsets[0]+len(raw)],
            'Preserved raw prefix differs from original source')
    return {'rawFrames':len(raw), 'rawSourceOffset':offsets[0],
            'rawMaximumError':0, 'workflowQualified':False}


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-installer-refresh.json').read_text())
    c = receipt['capsule']; archive = ROOT/'tests/results/X007'/c['name']
    require(archive.stat().st_size == c['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == c['sha256'], 'Refresh capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-refresh-') as temp:
        require(len(z.infolist()) == len(c['files']) <= 128 and
                {i.filename for i in z.infolist()} == set(c['files']) and
                sum(i.file_size for i in z.infolist()) <= 48*1024*1024,
                'Refresh capsule membership/bounds differ')
        root = Path(temp)
        for i in z.infolist():
            p = PurePosixPath(i.filename); row = c['files'][i.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                    ':' not in i.filename and p.as_posix() == i.filename and
                    p.suffix in {'.json','.stdout','.stderr','.txt','.wav','.f32','.lock'},
                    'Unsafe refresh evidence path')
            data = z.read(i)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'Refresh payload identity differs')
            dest = root.joinpath(*p.parts); dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(data)
        read = lambda name: json.loads((root/name).read_text(encoding='utf-8-sig'))
        prepared = read('prepared/receipt.json'); inputs = read('prepared/inputs.json')
        require(prepared['sourceHead'] == receipt['sourceHead'] and
                inputs['sourceCommit'] == receipt['nativeSourceEpoch'] and len(inputs['files']) == 344,
                'Prepared/native source epoch differs')
        verify_source_inputs(receipt['sourceHead'], inputs)
        verify_source_inputs(receipt['nativeSourceEpoch'], inputs)
        installed = read('attempt-1/result.json')
        require(all(installed[k] == 0 for k in ('installExit','uninstallExit','reinstallExit')) and
                installed['projectAndSettingsPreserved'] and
                all(installed[k][view]['Installed'] == 1 for k in ('runtimeBefore','runtimeAfter')
                    for view in ('view32','view64')), 'Installation/preservation or existing runtime differs')
        setup = [v for n,v in prepared['artifacts'].items() if n.endswith('-setup.exe')]
        require(len(setup) == 1 and setup[0]['sha256'] == installed['installerSha256'] == receipt['installerSha256'],
                'Installer identity differs')
        exe = prepared['payload']['soundcurrent-daw.exe']['sha256']
        for label in ('main','reinstalledMain'):
            main = installed[label]
            require(main['exitCode'] == 0 and main['exeSha256'] == exe and main['sessionId'] == 1 and
                    main['title'] == 'SoundCurrent DAW', 'Actual main normal close differs')
            modules(main['modules'], installed['installLocation'])
        require(len(installed['shortcuts']) == 2 and
                all(s['target'] == installed['installLocation']+'\\soundcurrent-daw.exe'
                    for s in installed['shortcuts']), 'Shortcut targets differ')
        tools = read('prepared/test-tools.json')
        for n in (1,2,3):
            result = read(f'attempt-{n}/result.json')
            execution(result, prepared, tools)
            require(result['installLocation'] == installed['installLocation'] and
                    (n == 1 or result['installedExeSha256'] == exe), 'Repeat product changed')
            actual = failure(root/f'attempt-{n}', receipt['failures'][str(n)])
            require(actual == receipt['rawEvidence'][str(n)], 'Independent raw evidence differs')
        cleanup = read('cleanup/result.json')
        require(cleanup['exitCode'] == cleanup['uninstallExit'] == cleanup['nativeProcessesRunning'] == 0 and
                cleanup['sourceHead'] == receipt['sourceHead'] and cleanup['sessionId'] == 1 and
                cleanup['installLocation'] == installed['installLocation'] and
                all(cleanup[k] for k in ('ownedProductRemoved','testOnlyToolsRemoved','projectsAndSettingsRetained')),
                'Owned installer cleanup differs')
        require(receipt['installedUiQualified'] is True and all(receipt[k] is False for k in
                ('installedNativeWorkflowQualified','cleanRuntimeBootstrapReplayed',
                 'captureDiscontinuityCauseIsolated','productionEqStopResolved',
                 'physicalOrSustainedQualified','releaseUploaded','fullParityPromoted')),
                'Refresh qualification promoted beyond its scope')
    print(json.dumps({'installedUiEvidenceVerified':True, 'captureFailuresRetained':3,
                      'installedNativeWorkflowQualified':False, 'nativeAudioReplayed':False}))


if __name__ == '__main__': run()
