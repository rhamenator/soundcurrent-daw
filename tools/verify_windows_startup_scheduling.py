#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Recompute retained startup correction evidence; never starts native audio."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from analyze_windows_startup import analyze
from verify_windows_desktop import verify as verify_desktop


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-startup-scheduling.json').read_text())
    inventory = receipt['capsule']; archive = ROOT/'tests/results/X007'/inventory['name']
    require(archive.stat().st_size == inventory['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == inventory['sha256'], 'Scheduling capsule identity differs')
    with zipfile.ZipFile(archive) as z, tempfile.TemporaryDirectory(prefix='sc-startup-scheduling-') as temp:
        require(len(z.infolist()) == len(inventory['files']) <= 128 and
                {i.filename for i in z.infolist()} == set(inventory['files']) and
                sum(i.file_size for i in z.infolist()) <= 32*1024*1024, 'Scheduling membership/bounds differ')
        root = Path(temp)
        for i in z.infolist():
            p = PurePosixPath(i.filename); row = inventory['files'][i.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                    ':' not in i.filename and p.as_posix() == i.filename and
                    p.suffix in {'.json','.log','.stdout','.stderr','.cpp','.hpp','.py','.wav','.f32','.lock'},
                    'Unsafe scheduling capsule path')
            data = z.read(i)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'Scheduling payload identity differs')
            target = root.joinpath(*p.parts); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        labels = {'v1': {'immediate-1','period-1','period-2'},
                  'v2': {'immediate','period','impulse','cancel-prepared','desktop','ui-full','controller','timing'}}
        for phase in ('v1','v2'):
            evidence = receipt['phases'][phase]
            manifest = json.loads((root/phase/'control/inputs.json').read_text())
            require(len(manifest['files']) == evidence['sourceInputFiles'] == 342 and
                    manifest['sourceCommit'] == evidence['sourceHead'], 'Scheduling source manifest differs')
            verify_source_inputs(evidence['sourceHead'],manifest)
            native = json.loads((root/phase/'native/result.json').read_text(encoding='utf-8-sig'))
            require(native == evidence['native'] and native['exitCode'] == 0 and native['sessionId'] == 1 and
                    len(native['tests']) == len(labels[phase]) and {t['label'] for t in native['tests']} == labels[phase] and
                    all(t['sessionId'] == 1 and t['exitCode'] == 0 for t in native['tests']), 'Native scheduling exits differ')
            audio = {'immediate-1','period-1','period-2'} if phase == 'v1' else {'immediate','period','impulse'}
            require(len({(t['exeSha256'],t['exeBytes']) for t in native['tests'] if t['label'] in audio}) == 1,
                    'Control/correction used different probe executables')
            for label in sorted(audio):
                actual = analyze(root/phase/'native'/label)
                retained = json.loads((root/phase/'analysis'/(label+'-analysis.json')).read_text())
                require(actual == retained == evidence['analyses'][label] and
                        actual['devicePeriod100ns'] == 100000 and actual['streamLatency100ns'] == 0 and
                        actual['after480MaximumResidual'] == 0, 'Independent scheduling analysis differs')
                if label.startswith('immediate'):
                    require(not actual['sourceFrameZeroPreserved'] and actual['nativeStartupFrames'] == 0 and
                            actual['firstAffectedFrame'] == 0 and actual['lastAffectedFrame'] == 479,
                            'Uncorrected control failure lost')
                else:
                    require(actual['sourceFrameZeroPreserved'] and actual['nativeStartupFrames'] == 480 and
                            actual['projectSourceStartsAtNativeFrame'] == 480 and actual['first480MaximumResidual'] == 0,
                            'Corrected content origin differs')
            require(b'100% tests passed' in (root/phase/'control/sanitizer-tests.log').read_bytes(),
                    'Retained sanitizer checks failed')
        cancelled = json.loads((root/'v2/native/cancel-prepared/probe.json').read_text())
        require(cancelled['format'] == 'sc-wasapi-prepared-cancel' and cancelled['nativeSdkAccepted'] and
                cancelled['sourceCallbacks'] == cancelled['submittedFrames'] == 0 and
                not cancelled['drained'] and not cancelled['diskWorkerCreated'] and
                cancelled['activationAfterStopRefused'] and cancelled['defaultsUnchanged'],
                'Prepared cancellation processed or completed audio')
        desktop_root = root/'v2/native/desktop'
        desktop_probe = json.loads((desktop_root/'probe.json').read_text())
        require(desktop_probe['nonSilentPlaybackStartRequired'] and desktop_probe['nativeStartupFrames'] == 480,
                'Desktop did not exercise a corrected non-silent start')
        actual = verify_desktop(desktop_root)
        require(actual == receipt['desktopIndependent'] ==
                json.loads((root/'v2/analysis/desktop-independent.json').read_text()) and
                actual['rawMaximumError'] == actual['exportMaximumError'] == actual['nativePlaybackMaximumResidual'] == 0,
                'Independent desktop media differs')
        require((root/'v2/native/timing.stdout').read_text(encoding='utf-8-sig').strip() ==
                '24 native startup timing checks passed' and
                b'100% tests passed' in (root/'linux/tests.log').read_bytes(), 'Timing/Linux checks differ')
        for p in (root/'source').rglob('*'):
            if not p.is_file(): continue
            name = p.relative_to(root/'source').as_posix()
            head = receipt['analysisSourceHead'] if name.endswith('.py') else receipt['phases']['v2']['sourceHead']
            require(subprocess.check_output(['git','show',head+':'+name],cwd=ROOT) == p.read_bytes(),
                    'Retained scheduling source differs')
        require(all(receipt[k] is False for k in ('installedPreviewsRebuilt','physicalOrSustainedTimingQualified',
                    'driverOrOsCauseIsolated','fullParityPromoted')), 'Correction proof promoted beyond scope')
    print(json.dumps({'retainedStartupSchedulingVerified':True,'ownedNonSilentStartupQualified':True,
                      'nativeAudioReplayed':False,'physicalDevicesQualified':False}))


if __name__ == '__main__': run()
