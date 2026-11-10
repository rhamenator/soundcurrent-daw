# SPDX-License-Identifier: GPL-3.0-only
"""Run four existing positive controller workflows with explicitly installed workers."""
import argparse
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import uuid
from installed_restoration_tests import digest, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'build', 'config', 'source', 'controller', 'asset', 'saved', 'live', 'manifest'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    build, source = Path(args.build).resolve(), Path(args.source).resolve()
    driver = Path(args.controller).resolve()
    evidence_root = build / 'installed-restoration-controller-evidence'
    evidence_root.mkdir(exist_ok=True)
    receipt = evidence_root / 'receipt.json'
    receipt.unlink(missing_ok=True)
    evidence = evidence_root / ('run-' + uuid.uuid4().hex)
    evidence.mkdir()
    require(driver.is_file(), 'Build sc-controller-restoration-tests first')
    workers = {name: Path(getattr(args, name)).resolve() for name in ('asset', 'saved', 'live', 'manifest')}
    require(all(p.is_file() for p in workers.values()), 'Build the four production worker targets first')
    environment = dict(os.environ, PATH=str(driver.parent) + os.pathsep + os.environ.get('PATH', ''))
    environment.pop('DESTDIR', None)
    with tempfile.TemporaryDirectory(prefix='installed controller workers ', dir=build) as temporary:
        prefix = Path(temporary)
        command = [args.cmake, '--install', str(build), '--prefix', str(prefix),
                   '--component', 'restoration-qualification']
        if args.config:
            command.extend(['--config', args.config])
        try:
            result = subprocess.run(command, capture_output=True, timeout=20, env=environment)
        except subprocess.TimeoutExpired as error:
            (evidence / 'install.stdout').write_bytes(error.stdout or b'')
            (evidence / 'install.stderr').write_bytes(error.stderr or b'')
            raise
        (evidence / 'install.stdout').write_bytes(result.stdout)
        (evidence / 'install.stderr').write_bytes(result.stderr)
        require(result.returncode == 0, 'Worker component installation failed')
        expected = {Path('bin')} | {Path('bin') / p.name for p in workers.values()}
        entries = {p.relative_to(prefix) for p in prefix.rglob('*')}
        (evidence / 'installed-files.json').write_text(json.dumps(sorted(map(str, entries)), indent=2) + '\n')
        require(entries == expected and not any(p.is_symlink() for p in prefix.rglob('*')),
                'Installed worker component entries differ')
        roles = []
        for name, original in workers.items():
            installed = prefix / 'bin' / original.name
            source_name = 'tools/asset_restoration_worker.cpp' if name == 'asset' else 'tools/saved_restoration_worker.cpp'
            row = dict(role=name, executable=original.name, executableSha256=digest(original),
                       installedSha256=digest(installed), source=source_name,
                       sourceSha256=digest(source / source_name))
            (evidence / (name + '.identity.json')).write_text(json.dumps(row, indent=2) + '\n')
            require(row['installedSha256'] == row['executableSha256'], 'Installed worker bytes differ: ' + name)
            if os.name != 'nt':
                require(stat.S_IMODE(installed.stat().st_mode) == 0o755, 'Installed worker mode differs: ' + name)
            roles.append(row)
        require(len({r['executableSha256'] for r in roles}) == 4, 'Worker roles share executable bytes')
        # Driver and checking/qualification dependencies remain in the build tree.
        # Only the four production ControllerOptions program defaults are redirected.
        command = [str(driver), '--installed-workers', str(prefix / 'bin')]
        (evidence / 'controller-command.json').write_text(json.dumps(command, indent=2) + '\n')
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, cwd=prefix, env=environment)
        try:
            stdout, stderr = process.communicate(timeout=120)
        except BaseException as error:
            process.kill()
            process.wait()
            (evidence / 'controller.stdout').write_bytes(getattr(error, 'output', None) or b'')
            (evidence / 'controller.stderr').write_bytes(getattr(error, 'stderr', None) or b'')
            (evidence / 'controller-process.json').write_text(json.dumps(
                dict(pid=process.pid, exitCode=process.returncode, failedBeforeCompletion=True), indent=2) + '\n')
            raise
        (evidence / 'controller.stdout').write_bytes(stdout)
        (evidence / 'controller.stderr').write_bytes(stderr)
        observation = dict(pid=process.pid, exitCode=process.returncode,
                           executableSha256=digest(driver),
                           sourceSha256=digest(source / 'tests/controller_restoration_tests.cpp'))
        (evidence / 'controller-process.json').write_text(json.dumps(observation, indent=2) + '\n')
        require(process.returncode == 0, 'Installed positive controller workflow failed')
        require(len(stdout) <= 1024 * 1024 and len(stderr) <= 1024 * 1024, 'Controller output extent exceeded')
        matches = re.findall(rb'(?m)^(\d+) actual installed controller-owned restoration checks passed; '
                             rb'four production roles; existing positive operation/PID/bytes/history guards\r?$', stdout)
        require(len(matches) == 1 and int(matches[0]) > 0, 'Installed controller workflow completion missing')
        require({p.relative_to(prefix) for p in prefix.rglob('*')} == expected
                and not any(p.is_symlink() for p in prefix.rglob('*')), 'Controller changed installed entries')
        require(all(digest(prefix / 'bin' / p.name) == digest(p) for p in workers.values()),
                'Controller changed installed executable bytes')
    receipt.write_text(json.dumps(dict(runDirectory=evidence.name, component='restoration-qualification',
                                      roles=roles, controller=observation, checks=int(matches[0]),
                                      positiveCases=['normalAndGuards', 'savedControllerStage0',
                                                     'savedControllerPublication0', 'manifestControllerStaging0'],
                                      exactFileSet=True, installedBytesMatch=True,
                                      installedControllerWorkflows=True,
                                      standaloneBundleQualified=False, installedGuiQualified=False), indent=2) + '\n')
    print('Four installed positive controller workflows passed with existing operation/PID/bytes/history guards')


if __name__ == '__main__':
    main()
