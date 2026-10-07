#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject replacement evidence and independently exercise archive member gates."""
import argparse
import hashlib
import io
import json
from pathlib import Path
from unittest.mock import patch
import warnings
import zipfile
import native_startup_portability_tests as portability


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, default=Path(__file__).resolve().parent /
                   'results/M2/native-manual-observations/2026-10-07-controlled-startup-evidence.zip')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    original = args.archive.read_bytes()
    portability.verify_archive_bytes(original)
    with zipfile.ZipFile(io.BytesIO(original)) as z:
        payload = {n: z.read(n) for n in z.namelist()}
    refused, member_refused = [], []
    for name in ['self-consistent-replacement', 'unmanifested-member', 'duplicate-member']:
        changed = payload.copy()
        if name == 'self-consistent-replacement':
            key = 'upstream-source-assessment-manifest.json'
            changed[key] += b'\n'
            m = json.loads(changed['manifest.json'])
            m['payload_files'][key] = hashlib.sha256(changed[key]).hexdigest()
            changed['manifest.json'] = (json.dumps(m, indent=2) + '\n').encode()
        if name == 'unmanifested-member':
            changed['unexpected.txt'] = b'Unrecorded archive member\n'
        packed = io.BytesIO()
        with zipfile.ZipFile(packed, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for member, content in changed.items():
                z.writestr(member, content)
            if name == 'duplicate-member':
                with warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    z.writestr('manifest.json', changed['manifest.json'])
        data = packed.getvalue()
        try:
            portability.verify_archive_bytes(data)
        except AssertionError as e:
            assert str(e) == 'Retained ZIP replaced'
            refused.append(name)
        else:
            raise AssertionError('Replacement archive accepted: ' + name)
        if name != 'self-consistent-replacement':
            # Isolate member checks after explicitly pinning this test fixture's
            # different hash. The production verifier's pin is never configurable.
            with patch.object(portability, 'RETAINED_ARCHIVE_SHA256', hashlib.sha256(data).hexdigest()):
                try:
                    portability.verify_archive_bytes(data)
                except AssertionError as e:
                    expected = 'Duplicate archive member' if name == 'duplicate-member' else 'Unexpected members'
                    assert str(e) == expected
                    member_refused.append(name)
                else:
                    raise AssertionError('Invalid member set accepted: ' + name)
    assert args.archive.read_bytes() == original
    result = {'replacement_archives_refused': refused, 'independent_member_gates_refused': member_refused,
              'original_archive_unchanged': True, 'native_audio_repeated': False}
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
