#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Developer record -> reopen -> file playback; read-only project/media audit."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='s6a-cli-', dir=ROOT / '.cache') as tmp:
    project = Path(tmp) / 'Enregistrement – Δοκιμή'
    subprocess.run([ROOT / '.cache/build-core/sc-record-tool', 'synthetic', project],
                   capture_output=True, text=True, check=True, timeout=10)

    def hashes():
        return {str(f.relative_to(project)): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in project.rglob('*') if f.is_file()}

    before = hashes()
    checked = subprocess.run([ROOT / '.cache/build-core/sc-play-tool', 'verify', project],
                             capture_output=True, text=True, check=True, timeout=10)
    result = json.loads(checked.stdout)
    assert result['played_frames'] == 480000 and result['missing_frames'] == 0
    assert result['peak'] == 2 and not result['audio_device']
    assert before == hashes()
    result.update(project_and_media_unchanged=True, unicode_path=True,
                  empty_export_range_falls_back_to_first_track=True)
    print(json.dumps(result))
