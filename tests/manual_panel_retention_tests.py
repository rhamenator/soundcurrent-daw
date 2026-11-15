# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the native manual-panel failure producer and bounded CI collector."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'failure_collector', ROOT / '.github/scripts/retain-controller-failure-projects.py')
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    args = parser.parse_args()
    evidence = args.build.resolve() / 'manual-panel-retention-evidence'
    evidence.mkdir(exist_ok=True)
    receipt = evidence / 'receipt.json'
    receipt.unlink(missing_ok=True)
    run = evidence / ('run-' + uuid.uuid4().hex)
    run.mkdir()
    command = [str(args.driver.resolve()), '--retention-failure-only']
    (run / 'command.json').write_text(json.dumps(command, indent=2) + '\n', encoding='utf-8')
    source = None
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, cwd=ROOT,
                                   env=dict(os.environ, QT_QPA_PLATFORM='offscreen'))
        stdout, stderr = b'', b''
        try:
            stdout, stderr = process.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            stdout, stderr = process.communicate()
            raise
        finally:
            (run / 'child.stdout').write_bytes(stdout)
            (run / 'child.stderr').write_bytes(stderr)
        assert len(stdout) <= 2 * 1024 * 1024 and len(stderr) <= 2 * 1024 * 1024
        marker = collector.MANUAL_PANEL_MARKER.encode('ascii')
        lines = [line for stream in (stdout, stderr)
                 for line in stream.splitlines() if line.startswith(marker)]
        assert len(lines) == 1
        candidate = Path(lines[0][len(marker):].decode('utf-8', errors='strict'))
        assert candidate.parent == Path(tempfile.gettempdir()) / collector.POLICIES[collector.MANUAL_PANEL_MARKER]
        assert collector.UUID.fullmatch(candidate.name)
        collector.plain(candidate.parent, True)
        collector.plain(candidate, True)
        source = candidate
        assert process.returncode == 1 and b'Injected manual panel retention failure' in stderr
        sandbox = run / 'fixture'
        logs = sandbox / 'Testing' / 'Temporary'
        logs.mkdir(parents=True)
        # Keep this expected failure out of the enclosing CTest log. Its raw
        # producer output and project copy remain in the evidence directory.
        (logs / 'LastTest.log').write_bytes(stdout + stderr)
        snapshots = collector.retain(sandbox)
        assert len(snapshots) == 1 and snapshots[0]['atomicProjectSnapshot'] is False
        files = snapshots[0]['originalFiles']
        assert files and any(row['path'].endswith('.wav') for row in files)
        saved = sandbox / 'controller-failure-projects' / ('manual-panel-' + source.name)
        for row in files:
            original, copied = source / row['path'], saved / row['path']
            assert original.stat().st_size == copied.stat().st_size == row['bytes']
            assert digest(original) == digest(copied) == row['sha256']
        try:
            collector.retain(sandbox)
        except ValueError as error:
            assert str(error) == 'Existing controller snapshot refused'
        else:
            raise AssertionError('Failure snapshot overwrite accepted')
        result = dict(nativePlatform=sys.platform, pid=process.pid, exitCode=process.returncode,
                      executableSha256=digest(args.driver),
                      sourceSha256=digest(ROOT / 'tests/manual_panel_tests.cpp'),
                      runDirectory=run.name, snapshotDirectory=str(saved.relative_to(evidence)),
                      snapshot=snapshots[0], exactCopiedBytes=True, overwriteRefused=True)
        shutil.rmtree(source)
        source = None
        receipt.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('Native manual-panel failure project retained with exact bytes and overwrite refusal')
    except BaseException:
        if source is not None:
            sys.stderr.buffer.write(
                (collector.MANUAL_PANEL_MARKER + str(source) + '\n').encode('utf-8'))
            sys.stderr.buffer.flush()
        raise


if __name__ == '__main__':
    main()
