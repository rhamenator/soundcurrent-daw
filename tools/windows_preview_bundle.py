#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain native Windows deployment inputs; never qualify an installer by collection."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APPLICATIONS = ('soundcurrent-daw.exe', 'sc-import-inspect-worker.exe',
                'sc-approved-wave-probe.exe', 'sc-media-import-worker.exe',
                'sc-stretch-render-worker.exe', 'sc-comp-span-worker.exe')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def pe_identity(path):
    size = path.stat().st_size
    require(128 <= size <= 64 * 1024 * 1024, 'PE file outside deployment bounds')
    with path.open('rb') as stream:
        header = stream.read(64)
        require(header[:2] == b'MZ', 'Not a PE executable')
        offset = struct.unpack_from('<I', header, 60)[0]
        require(64 <= offset <= size - 26, 'PE header outside file')
        stream.seek(offset)
        pe = stream.read(26)
    require(pe[:4] == b'PE\0\0' and struct.unpack_from('<H', pe, 4)[0] == 0x8664,
            'Preview requires native x64 PE files')
    require(struct.unpack_from('<H', pe, 24)[0] == 0x20b, 'Preview requires PE32+')
    return {'machine': 'x64', 'optionalHeader': 'PE32+',
            'linkTimestamp': struct.unpack_from('<I', pe, 8)[0]}


def audit_payload(stage, trusted):
    def linked(path):
        return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()
    require(stage.is_dir() and not linked(stage), 'Invalid deployment stage')
    expected = set(APPLICATIONS) | set(trusted['files'])
    found = {}
    for p in stage.rglob('*'):
        require(not linked(p), 'Deployment link/reparse point refused')
        if p.is_file():
            name = p.relative_to(stage).as_posix()
            require(name.casefold() not in {n.casefold() for n in found},
                    'Case-colliding deployment files')
            identity = pe_identity(p)
            found[name] = {'path': name, 'bytes': p.stat().st_size,
                           'sha256': digest(p), 'pe': identity}
    require(set(found) == expected, 'Unexpected or missing deployment file')
    require(sum(row['bytes'] for row in found.values()) <= 256 * 1024 * 1024,
            'Deployment exceeds retained bounds')
    mismatch = []
    for name, pin in trusted['files'].items():
        if any(found[name][k] != pin[k] for k in ('bytes', 'sha256')):
            mismatch.append(name)
    # CI compiles libsndfile. Its binary must not silently replace the product pin.
    require(set(mismatch) <= {'sndfile.dll'}, 'Qt runtime differs from reviewed SDK')
    return found, mismatch


def source_snapshot(expected):
    require(sys.platform == 'win32', 'Actual native Windows input capture required')
    git = lambda *a: subprocess.check_output(['git', *a], cwd=ROOT, text=True).strip()
    head, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    require(head == expected and not git('status', '--porcelain', '--untracked-files=no'),
            'Requested checkout is absent or dirty')
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0')
    names = sorted(n for n in names if n and not n.startswith('tests/results/'))
    return {'sourceCommit': head, 'sourceTree': tree,
            'files': [{'name': n, 'sha256': digest(ROOT / n)} for n in names],
            'excluded': ['tests/results/ historical evidence; available at the public commit']}


def collect(args):
    require(sys.platform == 'win32', 'Actual native Windows collection required')
    git = lambda *a: subprocess.check_output(['git', *a], cwd=ROOT, text=True).strip()
    head, tree = git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}')
    require(head == args.expected_source, 'Checkout differs from requested source commit')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'Tracked source dirty')
    build = args.build.resolve()
    cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
    home = [line.split('=', 1)[1] for line in cache.splitlines()
            if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    require(len(home) == 1 and Path(home[0]).resolve() == ROOT, 'Wrong CMake source root')
    require('CMAKE_BUILD_TYPE:STRING=Release' in cache, 'Release build required')
    trusted = json.loads((ROOT / 'research/windows-preview-dependencies.json').read_text())
    files, mismatch = audit_payload(args.stage, trusted)
    require(all(digest(build / name) == files[name]['sha256'] for name in APPLICATIONS),
            'Deployed application differs from native build')
    for name in ('sc-stretch-artifact-tests.exe', 'sc-protected-warp-probe.exe',
                 'sc-native-panel-tests.exe'):
        p = args.output / 'verification' / name
        require(p.is_file() and digest(p) == digest(build / name), 'Verifier changed: ' + name)
        pe_identity(p)
    test_pin = json.loads((ROOT / 'research/windows-desktop-test-dependencies.json').read_text())
    for name, key in [('Qt6Test.dll', 'bin/Qt6Test.dll'),
                      ('platforms/qoffscreen.dll', 'plugins/platforms/qoffscreen.dll')]:
        p = args.output / 'verification' / name
        pin = test_pin['files'][key]
        require(p.stat().st_size == pin['bytes'] and digest(p) == pin['sha256'],
                'Verification runtime differs from reviewed SDK: ' + name)
        pe_identity(p)
    # Input capture precedes compilation; require every current input to match it.
    inputs = json.loads(args.inputs.read_text(encoding='utf-8'))
    require(inputs == source_snapshot(args.expected_source), 'Source changed since pre-build capture')
    # Build/install source, including vendored code, fixtures, licenses and scripts.
    # Historical result media is not required to build the application; the full
    # public commit remains available as well. Never archive generated binaries.
    source_files = inputs['files']
    names = [row['name'] for row in source_files]
    out = args.output.resolve()
    (out / 'build-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n', encoding='utf-8')
    manifest = {'files': [{k: row[k] for k in ('path', 'bytes', 'sha256')}
                          for _, row in sorted(files.items())]}
    (out / 'deploy-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    with zipfile.ZipFile(out / 'deploy.zip', 'x', zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):
            z.write(args.stage / name, name)
    with tarfile.open(out / 'application-build-source.tar.gz', 'w:gz') as tar:
        for name in names:
            tar.add(ROOT / name, arcname='soundcurrent-daw/' + name, recursive=False)
    for n, pin in [('qtbase-everywhere-src-6.12.0.tar.xz', trusted['qtSourceSha256']),
                   ('libsndfile-1.2.2.tar.gz', trusted['sndfileSourceSha256'])]:
        require(digest(out / 'dependency-source' / n) == pin, 'Dependency source changed')
    require(git('rev-parse', 'HEAD') == head and git('rev-parse', 'HEAD^{tree}') == tree and
            not git('status', '--porcelain', '--untracked-files=no'), 'Source identity changed')
    require(all(digest(ROOT / row['name']) == row['sha256'] for row in source_files),
            'Source bytes changed during collection')
    require(all(digest(args.stage / name) == row['sha256'] for name, row in files.items()),
            'Deployment bytes changed during collection')
    receipt = {'format': 'sc-windows-preview-build-bundle-v1', 'sourceCommit': head,
               'sourceTree': tree, 'nativePlatform': sys.platform,
               'deploymentFiles': files, 'productDependencyMismatches': mismatch,
               'productDependenciesQualified': not mismatch,
               'mainWindowQualified': False, 'nativeAudioQualified': False,
               'installerPrepared': False, 'runtimeCompatibilityQualified': False,
               'sourceInputsUnchanged': True,
               'compiler': json.loads((out / 'compiler.json').read_text(encoding='utf-8-sig')),
               'artifacts': {p.relative_to(out).as_posix(): {'bytes': p.stat().st_size,
                                                           'sha256': digest(p)}
                             for p in out.rglob('*') if p.is_file() and
                             'deploy' not in p.relative_to(out).parts}}
    (out / 'collection.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'sourceCommit': head, 'productDependencyMismatches': mismatch,
                      'installerPrepared': False}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'stage', 'output', 'inputs'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--expected-source', required=True)
    p.add_argument('--snapshot-inputs', type=Path)
    args = p.parse_args()
    if args.snapshot_inputs:
        require(not args.snapshot_inputs.exists(), 'Pre-build snapshot exists')
        args.snapshot_inputs.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot_inputs.write_text(json.dumps(source_snapshot(args.expected_source), indent=2) + '\n', encoding='utf-8')
    else:
        require(all(getattr(args, n) is not None for n in ('build', 'stage', 'output', 'inputs')), 'Collection inputs required')
        collect(args)
