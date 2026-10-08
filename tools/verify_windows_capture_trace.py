#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only verification of retained native capture diagnostics, including failures."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zipfile
from verify_windows_installed_preview import require, verify_source_inputs

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
from analyze_windows_capture_trace import analyze

SOURCE = '097bbf9d759bcbf76a86902ccdb6ab613447ecd3'
FIXTURE = '4f6d228e927e29a5882c3d52859a05d2d08d7b94119d9ba5102e38fb13f5b876'
UNIT = '230a74ec42ed07470cec5af34477eff259853cbab3d559c2be67ee54eda10efe'


def executions(initial, captures, refused):
    require(initial['exitCode'] == 1 and initial['error'] == 'Unique owned render endpoint required' and
            [r['label'] for r in initial['tests']] == ['trace-unit','inventory'],
            'Initial endpoint ambiguity refusal was lost')
    require(captures['exitCode'] == 0 and captures['error'] is None and
            [r['label'] for r in captures['tests']] ==
            ['trace-unit','inventory','trace-1','trace-2','trace-3'] and
            [r['exitCode'] for r in captures['tests']] == [0,0,0,2,2],
            'Actual native capture exits/failures differ')
    for result in (initial,captures):
        require(result['sessionId'] == 1, 'Qualified native session differs')
        for row in result['tests']:
            unit = row['label'] == 'trace-unit'
            require(row['sessionId'] == 1 and row['processId'] > 0 and not row['timedOut'] and
                    row['exeSha256'] == (UNIT if unit else FIXTURE) and
                    row['exeBytes'] == (22016 if unit else 615936), 'Native executable/session differs')
        require(all(r['exitCode'] == 0 for r in result['tests'][:2]), 'Native prerequisites failed')
    require(refused['exitCode'] == 1 and refused['error'] == 'Expected limited interactive session 1' and
            refused['sessionId'] == 2 and len(refused['tests']) == 1,
            'Interrupted comparison refusal was lost')
    row = refused['tests'][0]
    require(row['label'] == 'trace-unit' and row['sessionId'] == 2 and row['processId'] == 6132 and
            row['exitCode'] is None and not row['timedOut'] and row['exeSha256'] == UNIT and
            row['exeBytes'] == 22016, 'Unknown child exit was promoted or comparison invented')


def claims(receipt):
    require(receipt['sourceHead'] == SOURCE and receipt['captureExitCodes'] == [0,2,2] and
            receipt['capturedRawFrames'] == [480000,215040,90240], 'Recorded capture scope differs')
    for key in ('installedWorkflowQualified','physicalAudioQualified','sustainedAudioQualified',
                'driverOrSchedulingCauseIsolated','cableComparisonQualified','fullSuiteParity',
                'previewRebuilt','publicBinaryUploaded','refusedTaskUnregistered'):
        require(receipt[key] is False, 'Unsupported qualification: '+key)


def unpack(archive, inventory, root):
    with zipfile.ZipFile(archive) as z:
        require(len(z.infolist()) == len(inventory) <= 128 and
                {i.filename for i in z.infolist()} == set(inventory) and
                sum(i.file_size for i in z.infolist()) <= 32*1024*1024,
                'Trace capsule membership/bounds differ')
        for i in z.infolist():
            p = PurePosixPath(i.filename); row = inventory[i.filename]
            require(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and
                    ':' not in i.filename and p.as_posix() == i.filename and
                    p.suffix in {'.json','.stdout','.stderr','.log','.wav','.f32','.lock'},
                    'Unsafe capture evidence path')
            data = z.read(i)
            require(len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'],
                    'Trace payload identity differs')
            dest = root.joinpath(*p.parts); dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(data)


def run():
    receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-capture-trace.json').read_text())
    claims(receipt)
    c = receipt['capsule']; archive = ROOT/'tests/results/X007'/c['name']
    require(archive.stat().st_size == c['bytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == c['sha256'], 'Trace capsule identity differs')
    with tempfile.TemporaryDirectory(prefix='sc-capture-trace-') as temp:
        root = Path(temp); unpack(archive,c['files'],root)
        read = lambda n: json.loads((root/n).read_text(encoding='utf-8-sig'))
        inputs = read('prepared/inputs.json')
        require(inputs['sourceCommit'] == SOURCE and len(inputs['files']) == 346,
                'Native source epoch/count differs')
        verify_source_inputs(SOURCE,inputs)
        build = read('prepared/build-observation.json')
        require(build['sourceHead'] == SOURCE and build['exitCode'] == 0,
                'Actual native build observation differs')
        executions(*(read('native/capture-trace-v'+str(i)+'/result.json') for i in (1,2,3)))
        cleanup = read('prepared/cleanup-observation.json')
        require(cleanup['cleanupSshExit'] == 255 and cleanup['binaryCollectionSshExit'] == 255 and
                cleanup['observedVmState'] == 'shut off' and not cleanup['restartedVm'] and
                not cleanup['refusedTaskUnregistered'] and cleanup['refusedTaskLastObservedState'] == 'Ready',
                'Refused task cleanup uncertainty lost')
        analyses = [analyze(root/'native/capture-trace-v2'/('trace-'+str(i))) for i in (1,2,3)]
        require([a['rawFrames'] for a in analyses] == receipt['capturedRawFrames'] and
                [a['ownedCaptureWorkflowQualified'] for a in analyses] == [True,False,False],
                'Capture qualification differs')
        for i,a in enumerate(analyses,1):
            require(a == read('analysis-'+str(i)+'.json'), 'Independent trace recomputation differs')
    print(json.dumps({'format':'sc-retained-capture-trace-verification-v1','captures':analyses,
                      'nativeAudioReplayed':False,'installedWorkflowQualified':False,
                      'refusedTaskCleanupPending':True},indent=2))


if __name__ == '__main__': run()
