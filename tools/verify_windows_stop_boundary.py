#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recompute retained direct Stop/end observations without Windows or audio replay."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from analyze_windows_stop import analyze


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-stop-boundary.json').read_text())
    inventory = receipt['capsule']; archive = ROOT/'tests/results/X007'/inventory['name']
    require(receipt['format'] == 'sc-windows-direct-stop-evidence-v1' and
            archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'], 'Stop capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-retained-stop-') as temp:
        require(len(z.infolist()) == len(inventory['files']) <= 64 and
                {i.filename for i in z.infolist()} == set(inventory['files']) and
                sum(i.file_size for i in z.infolist()) <= 12*1024*1024, 'Stop capsule bounds/membership differ')
        root = Path(temp)
        for i in z.infolist():
            p = PurePosixPath(i.filename); row = inventory['files'][i.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                    ':' not in i.filename and p.as_posix() == i.filename and
                    p.suffix in {'.json','.log','.stdout','.stderr','.cpp','.hpp','.py','.txt','.wav','.f32','.lock'},
                    'Unsafe Stop evidence path')
            data = z.read(i)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'Stop evidence payload differs')
            target = root.joinpath(*p.parts); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        manifest = json.loads((root/'control/inputs.json').read_text())
        require(manifest['sourceCommit'] == receipt['sourceHead'] and
                len(manifest['files']) == receipt['sourceInputFiles'] == 344, 'Stop source manifest differs')
        verify_source_inputs(receipt['sourceHead'],manifest)
        native = json.loads((root/'native/result.json').read_text(encoding='utf-8-sig'))
        require(native == receipt['native'] and native['exitCode'] == 0 and native['sessionId'] == 1 and
                len(native['tests']) == 3 and {t['label'] for t in native['tests']} ==
                {'cancel-1','cancel-2','non-silent-end'} and
                all(t['exitCode'] == 0 and t['sessionId'] == 1 for t in native['tests']) and
                len({(t['exeSha256'],t['exeBytes']) for t in native['tests']}) == 1,
                'Stop native execution identity differs')
        for label in ('cancel-1','cancel-2','non-silent-end'):
            actual = analyze(root/'native'/label)
            require(actual == receipt['analyses'][label] ==
                    json.loads((root/'analysis'/(label+'-analysis.json')).read_text()) and
                    actual['maximumResidual'] == 0 and actual['fixtureOffsetFrames'] == 544 and
                    actual['measuredInteriorGain'] == 1 and actual['nativeStartupFrames'] == 480 and
                    actual['capturedPrefixFidelityQualified'] and not actual['fullNonSilentEndQualified'],
                    'Stop sample observations differ')
            require(actual['unobservedSubmittedSourceFrames'] == (64 if label == 'non-silent-end' else 4864),
                    'Unobserved submitted source extent lost')
        observation = (root/'control/terminal-observation.log').read_bytes()
        require(re.search(rb'"state":\s*"Ready"',observation) and
                re.search(rb'"lastTaskResult":\s*0',observation) and
                b'Owned task still running; retain it and poll this task' in
                (root/'control/dispatch.log').read_bytes() and
                b'100% tests passed' in (root/'control/linux-analysis-tests.log').read_bytes(),
                'Actual task termination or observation race evidence lost')
        for p in (root/'source').rglob('*'):
            if p.is_file():
                name = p.relative_to(root/'source').as_posix()
                require(subprocess.check_output(['git','show',receipt['sourceHead']+':'+name],cwd=ROOT) == p.read_bytes(),
                        'Stop diagnostic source differs')
        require(all(receipt[k] is False for k in ('earlierProductionEqCancellationFailureResolved',
                    'fullNonSilentEndQualified','driverOrOsCauseIsolated','installedPreviewsRebuilt','fullParityPromoted')),
                'Stop diagnostic promoted beyond observations')
    print(json.dumps({'retainedDirectStopVerified':True,'directCapturedPrefixQualified':True,
                      'fullNonSilentEndQualified':False,'productionEqCancellationFailureResolved':False,
                      'nativeAudioReplayed':False}))


if __name__ == '__main__': run()
