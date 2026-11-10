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
    evidence = build / 'installed-restoration-evidence'
    evidence.mkdir(exist_ok=True)
    # Never leave a stale success receipt after a failed repeat.
    receipt = evidence / 'receipt.json'
    receipt.unlink(missing_ok=True)
    roles = [
        ('asset', 'sc-restoration-worker-error-v1', 'tools/asset_restoration_worker.cpp'),
        ('saved', 'sc-saved-staging-error-v1', 'tools/saved_restoration_worker.cpp'),
        ('live', 'sc-saved-restoration-error-v2', 'tools/saved_restoration_worker.cpp'),
        ('manifest', 'sc-saved-manifest-staging-error-v1', 'tools/saved_restoration_worker.cpp'),
    ]
    rows = []
    with tempfile.TemporaryDirectory(prefix='restoration install ', dir=build) as temporary:
        prefix = Path(temporary)
        command = [args.cmake, '--install', str(build), '--config', args.config,
                   '--prefix', str(prefix), '--component', 'restoration-qualification']
        install = subprocess.run(command, capture_output=True, timeout=20)
        (evidence / 'install.stdout').write_bytes(install.stdout)
        (evidence / 'install.stderr').write_bytes(install.stderr)
        require(install.returncode == 0, 'Qualification component install failed')
        expected = {Path('bin') / Path(getattr(args, role)).name for role, _, _ in roles}
        files = {p.relative_to(prefix) for p in prefix.rglob('*') if p.is_file()}
        require(files == expected, 'Qualification component file set differs')
        require(not any(p.is_symlink() for p in prefix.rglob('*')), 'Installed symlink refused')
        for role, protocol, source_name in roles:
            original = Path(getattr(args, role)).resolve()
            installed = prefix / 'bin' / original.name
            sha = digest(original)
            require(digest(installed) == sha, 'Installed executable bytes differ: ' + role)
            if os.name != 'nt':
                require(stat.S_IMODE(installed.stat().st_mode) == 0o755,
                        'Installed executable mode differs: ' + role)
            # Empty-object framing reaches each compiled protocol parser without
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
            (evidence / (role + '.stdout')).write_bytes(stdout)
            (evidence / (role + '.stderr')).write_bytes(stderr)
            require(process.returncode == 1 and stdout == b'', 'Invalid request was not refused: ' + role)
            require(len(stderr) <= 4096 and stderr.count(b'\n') == 1,
                    'Invalid refusal frame extent: ' + role)
            reply = json.loads(stderr)
            require(reply['protocol'] == protocol and type(reply['workerPid']) is int
                    and reply['workerPid'] == process.pid, 'Wrong native role/PID: ' + role)
            require(type(reply['errorCode']) is int, 'Missing error code: ' + role)
            if role == 'asset':
                require(reply['reconciliationRequired'] is False, 'Refusal touched destination')
            else:
                require(reply['published'] is False and reply['stagingMayExist'] is False,
                        'Refusal claimed staging or publication: ' + role)
                if role == 'live':
                    require(reply['publicationMayExist'] is False, 'Refusal claimed publication')
            rows.append(dict(role=role, executable=original.name, executableSha256=sha,
                             source=source_name, sourceSha256=digest(source / source_name),
                             nativePid=process.pid, exitCode=process.returncode,
                             refusal=reply))
        require(len({r['executableSha256'] for r in rows}) == 4, 'Worker roles share executable bytes')
    receipt.write_text(json.dumps(dict(component='restoration-qualification', roles=rows,
                                      exactFileSet=True, installedBytesMatch=True,
                                      nativeRefusalOnly=True,
                                      successfulRestorationQualified=False,
                                      installedGuiQualified=False), indent=2) + '\n')
    print('Four installed native worker roles refused invalid input with exact executable/PID binding')


if __name__ == '__main__':
    main()
