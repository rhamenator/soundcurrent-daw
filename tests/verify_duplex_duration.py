#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in paced 32-track synthetic duration/fault/recovery qualification.

Only launches/kills its own fixture child. No physical/native routing is used.
Failure artifacts preserve stdout/stderr and the owned project for inspection.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(binary, seconds, mode, diagnostics):
    folder = Path(tempfile.mkdtemp(prefix='sc-duration-', dir=ROOT / '.cache'))
    project = folder / 'Durée — Ελληνικά'
    diagnostics.update(mode=mode, seconds=seconds, project=str(project))
    before = time.monotonic()
    if mode == 'kill-target':
        child = subprocess.Popen([binary, project, str(seconds), mode], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        diagnostics['owned_pid'] = child.pid
        try:
            # The fixture only emits stdout after all writers crossed a checkpoint
            # interval. select() keeps the owner-side wait bounded on Linux.
            import select
            deadline = time.monotonic() + seconds + 30
            ready = ''
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    stdout, stderr = child.communicate(timeout=5)
                    diagnostics.update(stdout=stdout, stderr=stderr, exit_code=child.returncode)
                    raise AssertionError('Kill target exited before durable-prefix readiness')
                if select.select([child.stdout], [], [], .2)[0]:
                    ready = child.stdout.readline()
                    break
            assert ready and json.loads(ready)['kill_ready'], 'Kill target readiness timed out'
            assert json.loads(ready)['tracks'] == 32
            os.kill(child.pid, signal.SIGKILL)  # This freshly spawned owned child only.
            stdout, stderr = child.communicate(timeout=10)
            diagnostics.update(ready=ready, stdout=stdout, stderr=stderr,
                               exit_code=child.returncode, killed_owned_pid=True)
            diagnostics['killed_child'] = {'pid': child.pid, 'ready': json.loads(ready),
                                         'stdout': stdout, 'stderr': stderr,
                                         'exit_code': child.returncode}
            assert child.returncode == -signal.SIGKILL
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate(timeout=10)
        mode = 'verify-killed'
    try:
        completed = subprocess.run([binary, project, str(seconds), mode], capture_output=True,
                                   text=True, timeout=seconds + 120)
    except subprocess.TimeoutExpired as error:
        # subprocess.run kills and joins only its own child before raising. Its
        # partial output can be bytes even when text=True; retain it for diagnosis.
        def partial_text(value):
            return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value or ''
        diagnostics.update(stdout=partial_text(error.stdout), stderr=partial_text(error.stderr),
                           timed_out=True, wall_seconds=time.monotonic() - before)
        raise
    diagnostics.update(stdout=completed.stdout, stderr=completed.stderr,
                       exit_code=completed.returncode, wall_seconds=time.monotonic() - before)
    assert completed.returncode == 0, f'Owned duration fixture failed: {completed.stderr}'
    result = json.loads(completed.stdout)
    assert result['mode'] == mode and result['tracks'] == 32 and result['save_reopen']
    if mode == 'verify-killed':
        assert diagnostics['killed_owned_pid'] and result['originals_unchanged']
        assert result['minimum_recovered_frames'] > 0
    else:
        assert result['missing_track_frames'] == result['rt_allocations'] == result['rt_frees'] == result['rt_blocking_locks'] == 0
        assert not result['native_device_qualified']
        if mode == 'normal':
            assert result['minimum_raw_frames'] == result['maximum_raw_frames'] == seconds * 48000
            assert result['verified_raw_samples'] == 32 * seconds * 48000
            assert result['maximum_output_peak'] > 1, 'Float headroom never exercised'
            assert diagnostics['wall_seconds'] >= seconds, 'Synthetic device was not wall-paced'
    manifest = (project / 'project.json').read_bytes()
    result['project_manifest_sha256'] = hashlib.sha256(manifest).hexdigest()
    result['project_directory'] = str(project)
    result['supervisor_wall_seconds'] = diagnostics['wall_seconds']
    result['fixture_diagnostic'] = completed.stderr.strip()
    if mode == 'verify-killed':
        result['killed_child'] = diagnostics['killed_child']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=600)
    parser.add_argument('--modes', nargs='+', default=['normal'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--failure-output', type=Path, required=True)
    args = parser.parse_args()
    assert 1 <= args.seconds <= 1800
    (ROOT / '.cache').mkdir(exist_ok=True)
    results = []
    for mode in args.modes:
        diagnostics = {}
        try:
            results.append(run(args.binary.resolve(), args.seconds, mode, diagnostics))
        except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
            args.failure_output.write_text(json.dumps({'failure': str(error),
                'diagnostics': diagnostics, 'completed_cases': results}, indent=2, ensure_ascii=False) + '\n')
            raise
    encoded = json.dumps(results, indent=2, ensure_ascii=False) + '\n'
    args.output.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
