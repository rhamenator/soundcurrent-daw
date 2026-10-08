#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Authenticate/relocate retained evidence; no Windows/native audio execution."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import shutil
import struct
import tempfile
import zipfile
from verify_input_acquisition import require
from verify_windows_playback import verify

ROOT = Path(__file__).resolve().parents[1]
RECEIPT = ROOT / 'tests/results/X007/2026-10-08-windows-native-playback.json'


def extract(destination):
    receipt = json.loads(RECEIPT.read_text())
    archive = RECEIPT.with_name(receipt['archive'])
    require(archive.stat().st_size == receipt['archiveBytes'] and
            hashlib.sha256(archive.read_bytes()).hexdigest() == receipt['archiveSha256'], 'Archive hash/size differs')
    records = receipt['payloads']
    members = {r['path']:r for r in records}
    require(len(members) == len(records), 'Duplicate receipt member')
    with zipfile.ZipFile(archive) as z:
        require(len(z.namelist()) == len(members) and set(z.namelist()) == set(members) and
                z.testzip() is None, 'Archive member/CRC differs')
        for name, record in members.items():
            path = PurePosixPath(name)
            require(not path.is_absolute() and '..' not in path.parts and '\\' not in name,
                    'Unsafe archive member')
            data = z.read(name)
            require(len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256'],
                    'Payload hash/size differs')
            out = destination.joinpath(*path.parts)
            out.parent.mkdir(parents=True, exist_ok=True); out.write_bytes(data)
    return receipt


def main():
    with tempfile.TemporaryDirectory(prefix='sc-playback-relocation-') as tmp:
        root = Path(tmp); receipt = extract(root)
        normal = root / 'qualified/Playback-Δοκιμή'
        cancel = root / 'qualified/Cancel-Δοκιμή'
        verified = verify(normal); cancelled = verify(cancel)
        require(verified == receipt['normal'] and cancelled == receipt['cancel'], 'Relocated result differs')
        require('Clip/track layout mismatch' in (root / 'control/wasapi-playback-native-v1/rejection-reason.log').read_text()
                or 'Clip layout' in (root / 'control/wasapi-playback-native-v1/rejection-reason.log').read_text(),
                'Original model refusal missing')
        negatives = 0
        for kind in range(12):
            project = root / ('negative-' + str(kind)); shutil.copytree(normal, project)
            report_file = project / 'probe.json'; report = json.loads(report_file.read_text())
            if kind == 0: report['drained'] = False
            if kind == 1: report['nativePlaybackFailure'] = {'hresult': -1, 'operation': 2}
            if kind == 2: report['missingFrames'] = 1
            if kind == 3: report['cppAllocations'] = 1
            if kind == 4: report['liveFrame'] = 0
            if kind == 5: report['observations'][5]['clockPosition'] = 0
            if kind == 6: report['observations'][5]['submittedFrames'] += 1
            if kind == 7: report['capturedFrames'] -= 1
            if kind in (8, 9):
                capture = project / 'loopback' / report['capturePath']
                data = bytearray(capture.read_bytes()); at = data.index(b'data') + 8
                # Change one actual signal sample in the selected or unselected
                # channel; update its claimed hash to test the waveform gate.
                struct.pack_into('<f', data, at + (20000 * 2 + (kind == 9)) * 4, .25)
                capture.write_bytes(data); report['captureSha256'] = hashlib.sha256(data).hexdigest()
            if kind == 10:
                ref = project / 'expected-engine.f32'; data = bytearray(ref.read_bytes())
                struct.pack_into('<f', data, 20000 * 4, .25); ref.write_bytes(data)
            if kind == 11: report['capturePath'] = '../outside.wav'
            report_file.write_text(json.dumps(report))
            try: verify(project)
            except (ValueError, FileNotFoundError): negatives += 1
            else: raise ValueError('Altered claim/media accepted: ' + str(kind))
        print(json.dumps({'archivePayloads':len(receipt['payloads']), 'normal':verified,
                          'cancel':cancelled,'alteredClaimMediaRefusals':negatives,
                          'nativeAudioReplayed':False}, indent=2))


if __name__ == '__main__': main()
