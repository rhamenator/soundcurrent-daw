# SPDX-License-Identifier: GPL-3.0-only
"""Install four real worker roles and observe fail-closed native processes.

This is install/refusal evidence, not successful restoration, publication,
installer lifecycle, or permission to load a project in the installed GUI.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'build', 'config', 'source', 'asset', 'saved', 'live', 'manifest'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    build = Path(args.build).resolve()
    source = Path(args.source).resolve()
    evidence_root = build / 'installed-restoration-evidence'
    evidence_root.mkdir(exist_ok=True)
    evidence = evidence_root / ('run-' + uuid.uuid4().hex)
    evidence.mkdir()
    # Never leave a stale success receipt after a failed repeat.
    receipt = evidence_root / 'receipt.json'
    receipt.unlink(missing_ok=True)
    roles = [
        ('asset', 'sc-restoration-worker-error-v1', 'tools/asset_restoration_worker.cpp'),
        ('saved', 'sc-saved-staging-error-v1', 'tools/saved_restoration_worker.cpp'),
        ('live', 'sc-saved-restoration-error-v2', 'tools/saved_restoration_worker.cpp'),
        ('manifest', 'sc-saved-manifest-staging-error-v1', 'tools/saved_restoration_worker.cpp'),
    ]
    rows = []
    for role, _, _ in roles:
        require(Path(getattr(args, role)).is_file(), 'Build the production worker target first: ' + role)
    with tempfile.TemporaryDirectory(prefix='restoration install ', dir=build) as temporary:
        prefix = Path(temporary)
        command = [args.cmake, '--install', str(build),
                   '--prefix', str(prefix), '--component', 'restoration-qualification']
        if args.config:
            command.extend(['--config', args.config])
        install_environment = dict(os.environ)
        install_environment.pop('DESTDIR', None)
        try:
            install = subprocess.run(command, capture_output=True, timeout=20, env=install_environment)
        except subprocess.TimeoutExpired as error:
            (evidence / 'install.stdout').write_bytes(error.stdout or b'')
            (evidence / 'install.stderr').write_bytes(error.stderr or b'')
            raise
        (evidence / 'install.stdout').write_bytes(install.stdout)
        (evidence / 'install.stderr').write_bytes(install.stderr)
        require(install.returncode == 0, 'Qualification component install failed')
        expected = {Path('bin') / Path(getattr(args, role)).name for role, _, _ in roles}
        files = {p.relative_to(prefix) for p in prefix.rglob('*') if p.is_file()}
        (evidence / 'installed-files.json').write_text(json.dumps(
            sorted(str(p.relative_to(prefix)) for p in prefix.rglob('*')), indent=2) + '\n')
        require(files == expected and {p.relative_to(prefix) for p in prefix.rglob('*')}
                == expected | {Path('bin')}, 'Qualification component file set differs')
        require(not any(p.is_symlink() for p in prefix.rglob('*')), 'Installed symlink refused')
        for role, protocol, source_name in roles:
            original = Path(getattr(args, role)).resolve()
            installed = prefix / 'bin' / original.name
            sha = digest(original)
            installed_sha = digest(installed)
            (evidence / (role + '.identity.json')).write_text(json.dumps(
                dict(executable=original.name, buildExeSha256=sha, installedSha256=installed_sha),
                indent=2) + '\n')
            require(installed_sha == sha, 'Installed executable bytes differ: ' + role)
            if os.name != 'nt':
                require(stat.S_IMODE(installed.stat().st_mode) == 0o755,
                        'Installed executable mode differs: ' + role)
            # Empty-object framing reaches each compiled parser's completeness check without
            # providing a root, permit, operation, or publication authority.
            process = subprocess.Popen([str(installed)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       cwd=prefix, env=dict(os.environ, PATH=str(original.parent)
                                                          + os.pathsep + os.environ.get('PATH', '')))
            try:
                stdout, stderr = process.communicate(b'{}\n', timeout=5)
            except BaseException:
                process.kill()
                process.wait()
                raise
            (evidence / (role + '.process.json')).write_text(json.dumps(
                dict(role=role, nativePid=process.pid, exitCode=process.returncode,
                     executableSha256=sha, installedSha256=digest(installed)), indent=2) + '\n')
            (evidence / (role + '.stdout')).write_bytes(stdout)
            (evidence / (role + '.stderr')).write_bytes(stderr)
            require(process.returncode == 1 and stdout == b'', 'Invalid request was not refused: ' + role)
            require(len(stderr) <= 4096 and stderr.count(b'\n') == 1,
                    'Invalid refusal frame extent: ' + role)
            reply = json.loads(stderr)
            require(reply['protocol'] == protocol and type(reply['workerPid']) is int
                    and reply['workerPid'] == process.pid, 'Wrong native role/PID: ' + role)
            # ErrorCode::InvalidState is 1 (session.hpp); Canceled/framing or
            # resource-limit failures cannot substitute for incomplete-object refusal.
            require(type(reply['errorCode']) is int and reply['errorCode'] == 1,
                    'Wrong incomplete-request error code: ' + role)
            keys = {'protocol', 'workerPid', 'errorCode'}
            keys |= {'messageId', 'reconciliationRequired'} if role == 'asset' else {'published', 'stagingMayExist'}
            if role == 'live':
                keys.add('publicationMayExist')
            require(set(reply) == keys, 'Wrong refusal frame keys: ' + role)
            if role == 'asset':
                require(reply['messageId'] == 'restoration.worker_failed'
                        and reply['reconciliationRequired'] is False, 'Refusal touched destination')
            else:
                require(reply['published'] is False and reply['stagingMayExist'] is False,
                        'Refusal claimed staging or publication: ' + role)
                if role == 'live':
                    require(reply['publicationMayExist'] is False, 'Refusal claimed publication')
            after = {p.relative_to(prefix) for p in prefix.rglob('*') if p.is_file()}
            require(after == expected and {p.relative_to(prefix) for p in prefix.rglob('*')}
                    == expected | {Path('bin')}
                    and not any(p.is_symlink() for p in prefix.rglob('*')),
                    'Refusal changed installed file set: ' + role)
            require(all(digest(prefix / 'bin' / Path(getattr(args, name)).name)
                        == digest(Path(getattr(args, name))) for name, _, _ in roles),
                    'Refusal changed installed executable bytes: ' + role)
            rows.append(dict(role=role, executable=original.name, executableSha256=sha,
                             source=source_name, sourceSha256=digest(source / source_name),
                             nativePid=process.pid, exitCode=process.returncode,
                             refusal=reply))
        require(len({r['executableSha256'] for r in rows}) == 4, 'Worker roles share executable bytes')
    receipt.write_text(json.dumps(dict(runDirectory=evidence.name, component='restoration-qualification', roles=rows,
                                      exactFileSet=True, installedBytesMatch=True,
                                      nativeRefusalOnly=True,
                                      successfulRestorationQualified=False,
                                      installedGuiQualified=False), indent=2) + '\n')
    print('Four installed native worker roles refused invalid input with exact executable/PID binding')


if __name__ == '__main__':
    main()
