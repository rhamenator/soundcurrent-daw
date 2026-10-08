#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Relocated native desktop evidence and hostile evidence mutations; no audio replay."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import struct
import tempfile
import zipfile
from verify_windows_desktop import verify
from verify_input_acquisition import require

ROOT = Path(__file__).resolve().parents[1]
receipt = json.loads((ROOT/'tests/results/X007/2026-10-08-windows-desktop-workflow.json').read_text(encoding='utf-8'))
archive = ROOT/'tests/results/X007'/receipt['archive']['name']
require(archive.stat().st_size == receipt['archive']['bytes'] and
        hashlib.sha256(archive.read_bytes()).hexdigest() == receipt['archive']['sha256'],
        'Native desktop archive identity mismatch')
with tempfile.TemporaryDirectory(prefix='sc-native-desktop-evidence-') as temporary:
    base = Path(temporary)
    manifest = {f['path']:f for f in receipt['payloadManifest']}
    with zipfile.ZipFile(archive) as z:
        require(z.testzip() is None and len(z.infolist()) == len(manifest) and
                set(z.namelist()) == set(manifest), 'Native desktop CRC/membership mismatch')
        for n in z.namelist():
            name = PurePosixPath(n)
            require(not name.is_absolute() and '..' not in name.parts and '\\' not in n,
                    'Unsafe archive path')
            data = z.read(n)
            require(len(data) == manifest[n]['bytes'] and
                    hashlib.sha256(data).hexdigest() == manifest[n]['sha256'],
                    'Native desktop payload identity mismatch')
            p = base.joinpath(*name.parts)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
    project = base/receipt['finalAcceptedProject']
    report = verify(project)
    require(report == receipt['verification'], 'Relocated verification changed')
    original = base/receipt['originalStartupProject']
    observation = json.loads((base/'control/workflow-v10-startup-observation.json').read_text(encoding='utf-8'))
    require(json.loads((original/'probe.json').read_text(encoding='utf-8'))['nativeWorkflowAccepted'] and
            not observation['independentFullRangeAccepted'] and
            observation['first480MaximumResidual'] > .01 and
            observation['after480MaximumResidual'] == 0 and not observation['causeIsolated'],
            'Original startup discrepancy was suppressed/promoted')
    refused = 0
    for case in range(9):
        q = base/('mutation-'+str(case))
        shutil.copytree(project,q)
        probe = json.loads((q/'probe.json').read_text(encoding='utf-8'))
        model = json.loads((q/'project.json').read_text(encoding='utf-8'))
        raw = q/probe['rawPath']
        if case == 0: probe['nativeWorkflowAccepted'] = False
        if case == 1: probe['cppAllocations'] = 1
        if case == 2: probe['playbackEvents'][0]['frame'] += 480
        if case == 3:
            p = q/probe['exportPath'].replace('\\','/')
            data = bytearray(p.read_bytes()); data[-32:] = struct.pack('<8f',*[.03125]*8)
            p.write_bytes(data); probe['exportSha256'] = hashlib.sha256(data).hexdigest()
        if case == 4:
            data = bytearray(raw.read_bytes()); data[-32:] = struct.pack('<8f',*[.03125]*8)
            raw.write_bytes(data)
            model['assets'][0]['sha256'] = probe['rawSha256'] = hashlib.sha256(data).hexdigest()
        if case == 5:
            p = q/'source-stereo.f32'; data = bytearray(p.read_bytes()); data[-4:] = struct.pack('<f',.125)
            p.write_bytes(data)
        if case == 6:
            p = raw.parent/'journal.json'; data = json.loads(p.read_text(encoding='utf-8'))
            data['timingOrigin']['backend'] = 1; p.write_text(json.dumps(data))
        if case == 7: probe['exportPath'] = 'C:\\Users\\outside.wav'
        if case == 8: probe['frames'] -= 64
        (q/'project.json').write_text(json.dumps(model))
        (q/'probe.json').write_text(json.dumps(probe))
        try:
            verify(q)
        except ValueError:
            refused += 1
        else:
            raise ValueError('Altered native desktop evidence accepted: '+str(case))
    print(json.dumps({'archiveIntegrityVerified':True,'payloads':len(manifest),
                      'relocatedOneTrackNativeDesktopEvidence':True,
                      'alteredClaimsOrMediaRefused':refused,'originalStartupFailureRetained':True,
                      'nativeAudioReplayed':False,'installerQualified':False}))
