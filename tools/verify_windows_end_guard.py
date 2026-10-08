#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recompute retained normal-end qualification; no native audio replay."""
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
from verify_windows_desktop import verify as desktop
from verify_windows_playback import verify as playback


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-end-guard.json').read_text())
    inventory = receipt['capsule']; archive = ROOT/'tests/results/X007'/inventory['name']
    require(receipt['format'] == 'sc-windows-end-guard-evidence-v1' and
            archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'], 'End-guard capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-end-guard-') as temp:
        require(len(z.infolist()) == len(inventory['files']) <= 192 and
                {i.filename for i in z.infolist()} == set(inventory['files']) and
                sum(i.file_size for i in z.infolist()) <= 32*1024*1024, 'End-guard capsule bounds/membership differ')
        root = Path(temp)
        for i in z.infolist():
            p = PurePosixPath(i.filename); row = inventory['files'][i.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                    ':' not in i.filename and p.as_posix() == i.filename and
                    p.suffix in {'.json','.log','.stdout','.stderr','.cpp','.hpp','.py','.wav','.f32','.lock'},
                    'Unsafe end-guard evidence path')
            data = z.read(i)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'End-guard payload identity differs')
            target = root.joinpath(*p.parts); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        manifest = json.loads((root/'control/inputs.json').read_text())
        require(manifest['sourceCommit'] == receipt['sourceHead'] and
                len(manifest['files']) == receipt['sourceInputFiles'] == 344 and receipt['nativeBuildExit'] == 0,
                'End-guard source/build identity differs')
        verify_source_inputs(receipt['sourceHead'],manifest)
        native = json.loads((root/'native/result.json').read_text(encoding='utf-8-sig'))
        direct = {'control','guard-1','guard-2','one-frame','short','cancel-guard','cancel-active','cancel-prepared'}
        labels = direct | {'desktop','play-normal','play-cancel','ui-full','controller','timing','main'}
        require(native == receipt['native'] and native['exitCode'] == 0 and native['sessionId'] == 1 and
                len(native['tests']) == len(labels) and {t['label'] for t in native['tests']} == labels and
                all(t['exitCode'] == 0 and t['sessionId'] == 1 for t in native['tests']) and
                len({(t['exeSha256'],t['exeBytes']) for t in native['tests'] if t['label'] in direct}) == 1,
                'End-guard native execution identity differs')
        for label in sorted(direct - {'cancel-prepared'}):
            actual = analyze(root/'native'/label)
            require(actual == receipt['analyses'][label] ==
                    json.loads((root/'analysis'/(label+'-analysis.json')).read_text()) and
                    actual['maximumResidual'] == 0 and actual['fixtureOffsetFrames'] == 544 and
                    actual['measuredInteriorGain'] == 1 and actual['capturedPrefixFidelityQualified'],
                    'Independent direct end-guard samples differ')
            if label == 'control':
                require(not actual['fullNonSilentEndQualified'] and actual['unobservedSubmittedSourceFrames'] == 64 and
                        actual['endGuardSubmittedFrames'] == 0, 'Missing-tail control lost')
            elif label in ('cancel-guard','cancel-active'):
                require(not actual['fullNonSilentEndQualified'] and not actual['allSourceFramesObserved'] and
                        actual['unobservedSubmittedSourceFrames'] > 0 and
                        actual['endGuardSubmittedFrames'] == (480 if label == 'cancel-guard' else 0),
                        'Cancellation incorrectly became normal completion')
            else:
                require(actual['fullNonSilentEndQualified'] and actual['unobservedSubmittedSourceFrames'] == 0 and
                        actual['matchedFrames'] == (1 if label == 'one-frame' else 31 if label == 'short' else 96000) and
                        actual['endGuardSubmittedFrames'] == 480, 'Guarded source ending incomplete')
        prepared = json.loads((root/'native/cancel-prepared/probe.json').read_text())
        require(prepared['sourceCallbacks'] == prepared['submittedFrames'] == 0 and
                not prepared['drained'] and prepared['activationAfterStopRefused'], 'Prepared cancel processed audio')
        actual = desktop(root/'native/desktop')
        require(actual == receipt['analyses']['desktop'] and actual['matchedPlaybackFrames'] == 480000 and
                actual['rawMaximumError'] == actual['exportMaximumError'] == actual['nativePlaybackMaximumResidual'] == 0,
                'Guarded desktop workflow samples differ')
        actual = playback(root/'native/play-normal')
        require(actual == receipt['analyses']['play-normal'] and actual['matchedFrames'] == 192000 and
                actual['nativeMaximumResidual'] == 0 and actual['nativeQueueDrained'], 'Guarded production playback differs')
        try: playback(root/'native/play-cancel')
        except ValueError as e:
            require(str(e) == 'Native signal lost/repeated/altered', 'Production cancellation refusal changed')
        else: raise ValueError('Original production EQ cancellation failure disappeared without qualification')
        require(b'ValueError: Native signal lost/repeated/altered' in
                (root/'analysis/play-cancel-analysis.stderr').read_bytes(), 'Cancellation failure evidence lost')
        main = json.loads((root/'native/main-qualification.json').read_text(encoding='utf-8-sig'))
        require(main == next(t for t in native['tests'] if t['label'] == 'main') and
                (root/'native/timing.stdout').read_text(encoding='utf-8-sig').strip() ==
                '28 native boundary timing checks passed', 'Native main/timing result differs')
        for p in (root/'source').rglob('*'):
            if p.is_file():
                name = p.relative_to(root/'source').as_posix()
                require(subprocess.check_output(['git','show',receipt['sourceHead']+':'+name],cwd=ROOT) == p.read_bytes(),
                        'Frozen end-guard source differs')
        require(all(b'100% tests passed' in (root/'control'/name).read_bytes()
                    for name in ('linux-tests.log','sanitizer-tests.log')) and
                re.search(rb'"state":\s*"Ready"',(root/'control/terminal-observation.log').read_bytes()) and
                all(receipt[k] is False for k in ('earlierProductionEqCancellationFailureResolved',
                    'physicalOrSustainedTimingQualified','driverOrOsCauseIsolated','installedPreviewsRebuilt','fullParityPromoted')),
                'End-guard evidence promoted beyond its scope')
    print(json.dumps({'retainedEndGuardVerified':True,'ownedNativeNormalEndQualified':True,
                      'nativeDesktopSourceExtentQualified':True,'productionEqCancellationFailureResolved':False,
                      'nativeAudioReplayed':False,'installerQualified':False}))


if __name__ == '__main__': run()
