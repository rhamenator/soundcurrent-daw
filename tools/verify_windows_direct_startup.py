#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained direct-render observations; no native playback or VM access."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from analyze_windows_startup import analyze


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-direct-startup.json').read_text())
    inventory = receipt['capsule']; archive = ROOT/'tests/results/X007'/inventory['name']
    require(archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'], 'Startup capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-direct-startup-') as temp:
        require(len(z.infolist()) == len(inventory['files']) <= 64 and
                {m.filename for m in z.infolist()} == set(inventory['files']) and
                sum(m.file_size for m in z.infolist()) <= 16*1024*1024, 'Startup membership/bounds differ')
        root = Path(temp)
        for m in z.infolist():
            p = PurePosixPath(m.filename); row = inventory['files'][m.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in m.filename and
                    ':' not in m.filename and p.as_posix() == m.filename and
                    p.suffix in {'.json','.log','.stdout','.stderr','.cpp','.py','.wav','.f32','.lock'},
                    'Unsafe startup capsule path')
            data = z.read(m)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'Startup payload identity differs')
            target = root.joinpath(*p.parts); target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data)
        manifest = json.loads((root/'control/inputs.json').read_text())
        require(len(manifest['files']) == receipt['sourceInputFiles'] == 338 and
                manifest['sourceCommit'] == receipt['nativeSourceHead'], 'Native startup source count differs')
        verify_source_inputs(receipt['nativeSourceHead'], manifest)
        # The analyzer's stricter timing-origin check and synthetic tests were
        # committed after the native binary; retain that separate revision.
        import subprocess
        for name in ('tests/wasapi_startup_fixture.cpp','tests/analyze_windows_startup.py',
                     'tests/windows_startup_analysis_tests.py'):
            head = receipt['nativeSourceHead'] if name.endswith('.cpp') else receipt['analysisSourceHead']
            data = subprocess.check_output(['git','show',head+':'+name],cwd=ROOT)
            require(data == (root/'source'/name).read_bytes(), 'Retained probe/analyzer source differs')
        native = json.loads((root/'native/result.json').read_text(encoding='utf-8-sig'))
        labels = {'immediate-1','silent-lead','immediate-2'}
        require(native == receipt['native'] and native['exitCode'] == 0 and native['sessionId'] == 1 and
                len(native['tests']) == 3 and {t['label'] for t in native['tests']} == labels and
                all(t['exitCode'] == 0 and t['sessionId'] == 1 for t in native['tests']) and
                len({(t['exeSha256'],t['exeBytes']) for t in native['tests']}) == 1,
                'Native startup identity/exit differs')
        for label in sorted(labels):
            actual = analyze(root/'native'/label)
            retained = json.loads((root/'analysis'/(label+'.json')).read_text())
            require(actual == retained == receipt['analyses'][label], 'Independent startup analysis differs')
            require(actual['measuredInteriorGain'] == 1 and actual['after480MaximumResidual'] == 0 and
                    actual['driverOrOsCauseIsolated'] is False, 'Startup scope/interior differs')
            if label.startswith('immediate'):
                require(actual['silentLeadFrames'] == 0 and actual['directPathAlterationObserved'] and
                        actual['firstAffectedFrame'] == 0 and actual['lastAffectedFrame'] == 479,
                        'Retained non-silent startup failure lost')
            else:
                require(actual['silentLeadFrames'] == 12000 and not actual['directPathAlterationObserved'],
                        'Silent-lead comparison differs')
        require(all(receipt[k] is False for k in ('installedPreviewsRebuilt','physicalOrSustainedTimingQualified',
                    'driverOrOsCauseIsolated','parityPromoted','productWorkaroundAdded')),
                'Startup diagnosis promoted beyond its scope')
    print(json.dumps({'retainedDirectStartupVerified':True,'nativeAudioReplayed':False,
                      'startupFailureResolved':False}))


if __name__ == '__main__':
    run()
