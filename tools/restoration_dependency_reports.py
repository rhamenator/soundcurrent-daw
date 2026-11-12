#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture bounded native inspector reports; never qualify dependency closure."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

from restoration_worker_images import digest, linked, read, require, verify_staged_payload

MAX_LOG = 1024 * 1024


def run_report(argv, stdout, stderr, *, timeout=30, maximum=MAX_LOG):
    """Bound both pipe drains before writing; stop only this inspector on refusal."""
    require(type(maximum) is int and 0 < maximum <= MAX_LOG
            and type(timeout) in (int, float) and 0 < timeout <= 30, 'Invalid inspector resource bounds')
    errors = []
    started = time.monotonic()
    with stdout.open('xb') as out, stderr.open('xb') as err:
        process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def drain(stream, output):
            count = 0
            try:
                while True:
                    block = stream.read(65536)
                    if not block:
                        break
                    count += len(block)
                    if count > maximum:
                        errors.append('Inspector output exceeds bound')
                        process.kill()
                        break
                    output.write(block)
            except Exception as error:
                errors.append(type(error).__name__)
                if process.poll() is None:
                    process.kill()

        threads = [threading.Thread(target=drain, args=pair, daemon=True)
                   for pair in [(process.stdout, out), (process.stderr, err)]]
        for thread in threads:
            thread.start()
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            errors.append('Inspector deadline exceeded')
            process.kill()
            code = process.wait(timeout=5)
        for thread in threads:
            thread.join(timeout=5)
        require(not any(t.is_alive() for t in threads), 'Inspector pipe did not close')
        process.stdout.close()
        process.stderr.close()
    result = dict(exitCode=code, seconds=time.monotonic() - started,
                  stdoutBytes=stdout.stat().st_size, stderrBytes=stderr.stat().st_size,
                  errors=errors)
    stdout.with_suffix('.result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    require(not errors and code == 0 and result['stdoutBytes'] > 0, 'Inspector failed; preserve pending reports')
    return result


def resolve_inspector(platform, inspector=None):
    require(platform in ('linux', 'win32'), 'Unsupported inspector platform')
    name = 'readelf' if platform == 'linux' else 'dumpbin.exe'
    selected = inspector or shutil.which(name)
    require(selected is not None, 'Native dependency inspector unavailable: ' + name)
    tool = Path(selected).resolve(strict=True)
    require(tool.is_file() and not linked(tool) and 0 < tool.stat().st_size <= 64 * 1024 * 1024,
            'Invalid native inspector')
    require((platform == 'linux' and tool.name.endswith('readelf'))
            or (platform == 'win32' and tool.name.lower() == name), 'Unexpected native inspector name')
    return tool


def capture_dependencies(candidate, output, *, expected_source, platform,
                         collection_sha256, candidate_sha256, inspector=None):
    """Read pinned helper bytes with the host inspector, without running helpers."""
    require(platform == sys.platform, 'Dependency reports require the actual native host')
    verified = verify_staged_payload(candidate, expected_source=expected_source, platform=platform,
                                    collection_sha256=collection_sha256, candidate_sha256=candidate_sha256)
    tool = resolve_inspector(platform, inspector)
    tool_hash = digest(tool, 64 * 1024 * 1024)
    require(not output.exists() and not linked(output) and output.parent.is_dir()
            and not linked(output.parent) and not output.resolve().is_relative_to(candidate.resolve()),
            'Existing, linked or nested report output')
    output.mkdir(mode=0o700)
    pending = output / 'reports.pending.json'
    pending.write_text(json.dumps(dict(format='sc-restoration-dependency-reports-pending-v1',
                                      sourceCommit=expected_source)) + '\n', encoding='utf-8')
    reports = []
    for row in verified['payload']:
        image = (candidate / row['stagedPath']).resolve(strict=True)
        argv = [str(tool), '--dynamic', '--wide', str(image)] if platform == 'linux' else [str(tool), '/nologo', '/dependents', str(image)]
        name = row['role']
        command = output / (name + '.command.json')
        command.write_text(json.dumps(dict(argv=argv, imageSha256=row['sha256']), indent=2) + '\n', encoding='utf-8')
        result = run_report(argv, output / (name + '.stdout'), output / (name + '.stderr'))
        reports.append(dict(role=name, imagePath=row['stagedPath'], imageBytes=row['bytes'],
                            imageSha256=row['sha256'], exitCode=result['exitCode'],
                            stdout=name + '.stdout', stderr=name + '.stderr',
                            command=name + '.command.json', result=name + '.result.json'))
    require(verify_staged_payload(candidate, expected_source=expected_source, platform=platform,
                                 collection_sha256=collection_sha256, candidate_sha256=candidate_sha256) == verified,
            'Candidate changed during inspection')
    require(digest(tool, 64 * 1024 * 1024) == tool_hash, 'Inspector changed during capture')
    expected = {field for row in reports for key, field in row.items()
                if key in ('stdout', 'stderr', 'command', 'result')}
    files = list(output.iterdir())
    require(len(files) == 17 and {p.name for p in files} == expected | {pending.name}
            and all(p.is_file() and not linked(p) for p in files), 'Report inventory differs')
    bindings = [dict(path=name, bytes=(output / name).stat().st_size,
                     sha256=digest(output / name, MAX_LOG)) for name in sorted(expected)]
    result = dict(format='sc-restoration-dependency-reports-v1', sourceCommit=expected_source,
                  nativePlatform=platform, collectionSha256=collection_sha256, candidateSha256=candidate_sha256,
                  inspector=dict(path=str(tool), bytes=tool.stat().st_size, sha256=tool_hash),
                  reports=reports, fileBindings=bindings, reportsCaptured=True,
                  nativeExecutionPerformed=False, dependencyClosureQualified=False,
                  dependencySourceBundleQualified=False, standaloneBundleQualified=False,
                  packageAdmissionGranted=False, installerQualified=False, installedGuiQualified=False)
    raw = (json.dumps(result, indent=2) + '\n').encode('utf-8')
    pending.write_bytes(raw)
    require(read(pending) == raw, 'Dependency receipt readback differs')
    pending.rename(output / 'reports.json')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--collection-sha256', required=True)
    parser.add_argument('--candidate-sha256', required=True)
    parser.add_argument('--inspector', type=Path)
    args = parser.parse_args()
    print(json.dumps(capture_dependencies(args.candidate, args.output, expected_source=args.expected_source,
                     platform=sys.platform, collection_sha256=args.collection_sha256,
                     candidate_sha256=args.candidate_sha256, inspector=args.inspector)))
