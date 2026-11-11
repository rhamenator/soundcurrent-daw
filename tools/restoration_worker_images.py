#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect native worker images and repository source; never qualify an installer."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
ROLES = {'asset': ('sc-asset-restoration-worker', 'tools/asset_restoration_worker.cpp'),
         'saved': ('sc-saved-restoration-worker', 'tools/saved_restoration_worker.cpp'),
         'live': ('sc-saved-restoration-live-worker', 'tools/saved_restoration_worker.cpp'),
         'manifest': ('sc-saved-manifest-worker', 'tools/saved_restoration_worker.cpp')}
CASES = ['normalAndGuards', 'savedControllerStage0', 'savedControllerPublication0', 'manifestControllerStaging0']
EXCLUDED = ['tests/results/ historical evidence; available at the public commit']


def require(value, message):
    if not value:
        raise RuntimeError(message)


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path, maximum=2 * 1024 * 1024):
    require(path.is_file() and not linked(path) and path.stat().st_size <= maximum, 'Missing/linked/oversized evidence: ' + path.name)
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, 'Evidence grew beyond bound')
    return data


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate evidence key')
            result[key] = value
        return result
    return json.loads(read(path), object_pairs_hook=unique)


def git(root, *args):
    return subprocess.check_output(['git', *args], cwd=root, text=True).strip()


def source_snapshot(root, expected):
    require(re.fullmatch('[0-9a-f]{40}', expected), 'Invalid requested source commit')
    require(git(root, 'rev-parse', 'HEAD') == expected and not git(root, 'status', '--porcelain', '--untracked-files=no'),
            'Requested source is absent or tracked inputs changed')
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=root).decode('utf-8').split('\0')
    names = sorted(n for n in names if n and not n.startswith('tests/results/'))
    for name in names:
        path = PurePosixPath(name)
        require(not path.is_absolute() and '..' not in path.parts and (root / name).is_file()
                and not linked(root / name), 'Invalid tracked source input')
    return {'sourceCommit': expected, 'sourceTree': git(root, 'rev-parse', 'HEAD^{tree}'),
            'files': [{'name': n, 'sha256': digest(root / n)} for n in names], 'excluded': EXCLUDED}


def native_identity(path, platform):
    with path.open('rb') as stream:
        header = stream.read(4096)
    if platform == 'linux':
        require(len(header) >= 20 and header[:6] == b'\x7fELF\x02\x01'
                and struct.unpack_from('<H', header, 18)[0] == 62, 'Not a native Linux amd64 image')
        return 'ELF64-amd64'
    require(platform == 'win32' and len(header) >= 64 and header[:2] == b'MZ', 'Not a native Windows image')
    offset = struct.unpack_from('<I', header, 60)[0]
    require(offset <= len(header) - 6 and header[offset:offset + 4] == b'PE\0\0'
            and struct.unpack_from('<H', header, offset + 4)[0] == 0x8664, 'Not a native Windows amd64 PE image')
    return 'PE-amd64'


def materials(build, source, inputs, platform):
    require(platform in ('linux', 'win32'), 'Actual Linux or Windows collection required')
    require(build.is_dir() and not linked(build), 'Invalid native build directory')
    cache = read(build / 'CMakeCache.txt').decode('utf-8')
    home = [s.split('=', 1)[1] for s in cache.splitlines() if s.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    require(len(home) == 1 and Path(home[0]).resolve() == source.resolve(), 'Wrong CMake source root')
    receipt_path = build / 'installed-restoration-controller-evidence/receipt.json'
    data = read_json(receipt_path)
    require(data['component'] == 'restoration-qualification' and data['positiveCases'] == CASES
            and data['exactFileSet'] is True and data['installedBytesMatch'] is True
            and data['installedControllerWorkflows'] is True and data['standaloneBundleQualified'] is False
            and data['installedGuiQualified'] is False, 'Wrong installed controller evidence')
    require(re.fullmatch('run-[0-9a-f]{32}', data['runDirectory']), 'Invalid evidence run directory')
    records = receipt_path.parent / data['runDirectory']
    require(records.is_dir() and not linked(records), 'Linked/missing evidence directory')
    driver = data['controller']
    pins = {row['name']: row['sha256'] for row in inputs['files']}
    driver_path = build / ('sc-controller-restoration-tests.exe' if platform == 'win32' else 'sc-controller-restoration-tests')
    require(driver['sourceSha256'] == pins['tests/controller_restoration_tests.cpp']
            and type(driver['pid']) is int and driver['pid'] > 0
            and type(driver['exitCode']) is int and driver['exitCode'] == 0
            and driver_path.is_file() and not linked(driver_path) and digest(driver_path) == driver['executableSha256']
            and read_json(records / 'controller-process.json') == driver, 'Wrong native controller process/source')
    command = read_json(records / 'controller-command.json')
    require(isinstance(command, list) and len(command) == 3 and command[1] == '--installed-workers'
            and Path(command[0]).resolve() == driver_path.resolve()
            and Path(command[2]).is_absolute() and Path(command[2]).name == 'bin'
            and Path(command[2]).parent.parent.resolve() == build.resolve(), 'Wrong installed controller command')
    output = read(records / 'controller.stdout', 1024 * 1024)
    read(records / 'controller.stderr', 1024 * 1024)
    matches = re.findall(rb'(?m)^(\d+) actual installed controller-owned restoration checks passed; four production roles; existing positive operation/PID/bytes/history guards\r?$', output)
    require(len(matches) == 1 and type(data['checks']) is int and int(matches[0]) == data['checks'] > 0,
            'Native positive controller completion missing')
    require(len(data['roles']) == 4 and {row['role'] for row in data['roles']} == set(ROLES), 'Wrong production role set')
    rows = []
    for row in data['roles']:
        name, source_name = ROLES[row['role']]
        name += '.exe' if platform == 'win32' else ''
        path = build / name
        require(path.is_file() and not linked(path) and 0 < path.stat().st_size <= 256 * 1024 * 1024,
                'Missing/linked/oversized native image')
        require(row['executable'] == name and row['source'] == source_name
                and row['sourceSha256'] == pins[source_name]
                and digest(path) == row['executableSha256'] == row['installedSha256']
                and read_json(records / (row['role'] + '.identity.json')) == row, 'Native image/source differs from positive evidence')
        rows.append(dict(role=row['role'], path=name, bytes=path.stat().st_size,
                         sha256=row['executableSha256'], nativeIdentity=native_identity(path, platform),
                         source=source_name, sourceSha256=row['sourceSha256']))
    require(len({row['sha256'] for row in rows}) == 4 and sum(row['bytes'] for row in rows) <= 512 * 1024 * 1024,
            'Repeated or excess production images')
    return data, records, sorted(rows, key=lambda row: row['role'])


def audit_source(path, inputs):
    expected = {'soundcurrent-daw/' + row['name']: row['sha256'] for row in inputs['files']}
    seen = set()
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            require(member.isfile() and member.name in expected and member.name not in seen, 'Unexpected source archive entry')
            seen.add(member.name)
            with archive.extractfile(member) as stream:
                require(hashlib.file_digest(stream, 'sha256').hexdigest() == expected[member.name], 'Source archive bytes differ')
    require(seen == set(expected), 'Source archive inputs missing')


def package_profile(rows, inputs, platform, *, build_type, cache_sha256, positive_receipt_sha256):
    """Bind an explicit helper-only payload plan, without granting package admission."""
    require(platform in ('linux', 'win32'), 'Unsupported package profile platform')
    require(build_type in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')
            and all(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value)
                    for value in (cache_sha256, positive_receipt_sha256)), 'Invalid package build context')
    require(isinstance(inputs, dict) and isinstance(inputs.get('files'), list)
            and all(isinstance(row, dict) and set(row) == {'name', 'sha256'}
                    and isinstance(row['name'], str) and isinstance(row['sha256'], str)
                    and re.fullmatch('[0-9a-f]{64}', row['sha256']) for row in inputs['files']),
            'Invalid package source inputs')
    require(all(isinstance(inputs.get(key), str) and re.fullmatch('[0-9a-f]{40}', inputs[key])
                for key in ('sourceCommit', 'sourceTree')), 'Invalid package source identity')
    pins = {row['name']: row['sha256'] for row in inputs['files']}
    require(len(pins) == len(inputs['files']), 'Duplicate package source input')
    require(isinstance(rows, list) and len(rows) == 4
            and all(isinstance(row, dict) and set(row) ==
                    {'role', 'path', 'bytes', 'sha256', 'nativeIdentity', 'source', 'sourceSha256'}
                    and isinstance(row['role'], str) for row in rows)
            and {row['role'] for row in rows} == set(ROLES), 'Wrong package role set/fields')
    payload = []
    for row in sorted(rows, key=lambda row: row['role']):
        name, source = ROLES[row['role']]
        name += '.exe' if platform == 'win32' else ''
        identity = 'PE-amd64' if platform == 'win32' else 'ELF64-amd64'
        require(row['path'] == name and row['source'] == source and source in pins
                and row['sourceSha256'] == pins[source]
                and isinstance(row['sourceSha256'], str) and isinstance(row['sha256'], str)
                and re.fullmatch('[0-9a-f]{64}', row['sourceSha256'])
                and re.fullmatch('[0-9a-f]{64}', row['sha256'])
                and row['nativeIdentity'] == identity and type(row['bytes']) is int
                and 0 < row['bytes'] <= 256 * 1024 * 1024, 'Package role/image/source mismatch')
        payload.append(dict(row, payloadPath=name if platform == 'win32' else 'usr/bin/' + name))
    require(len({row['sha256'] for row in payload}) == 4
            and sum(row['bytes'] for row in payload) <= 512 * 1024 * 1024, 'Aliased/excess package images')
    return dict(format='sc-restoration-package-profile-v1', profile='restoration-workers-v1',
                sourceCommit=inputs['sourceCommit'], sourceTree=inputs['sourceTree'], nativePlatform=platform,
                buildType=build_type, cmakeCacheSha256=cache_sha256,
                positiveReceiptSha256=positive_receipt_sha256,
                payload=payload, helperOnly=True, defaultPreviewInventoryChanged=False,
                dependencySourceBundleQualified=False, standaloneBundleQualified=False,
                installerQualified=False, installedGuiQualified=False)


def collect(args, root=ROOT, platform=sys.platform):
    inputs = read_json(args.inputs)
    require(inputs == source_snapshot(root, args.expected_source), 'Source differs from pre-build capture')
    require(args.build.is_dir() and not linked(args.build), 'Invalid native build directory')
    cache = read(args.build / 'CMakeCache.txt').decode('utf-8')
    homes = [line.split('=', 1)[1] for line in cache.splitlines() if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    require(len(homes) == 1 and Path(homes[0]).resolve() == root.resolve(), 'Wrong CMake source root')
    types = [line.split('=', 1)[1] for line in cache.splitlines() if line.startswith('CMAKE_BUILD_TYPE:STRING=')]
    require(len(types) == 1 and types[0] in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'), 'Missing explicit native build configuration')
    if args.allow_absent:
        plan = read_json(args.build / 'ci-shard-plan.json')
        shards, selected = plan['shards'], plan['shard']
        require(platform == 'linux' and type(selected) is int and 0 <= selected < len(shards)
                and plan['allTestsAssignedExactlyOnce'] is True and type(plan['exitCode']) is int
                and plan['exitCode'] == 0, 'Incomplete native shard execution')
        names = [name for shard in shards for name in shard['names']]
        require(len(names) == len(set(names)) == plan['inventoryCount']
                and names.count('installed-restoration-controller') == 1, 'Missing/duplicate positive controller shard assignment')
        owns_test = 'installed-restoration-controller' in shards[selected]['names']
        has_receipt = (args.build / 'installed-restoration-controller-evidence/receipt.json').exists()
        require(owns_test == has_receipt, 'Positive controller collection missing or on wrong shard')
        if not owns_test:
            print(json.dumps({'collected': False, 'reason': 'Positive controller test assigned to another successful shard'}))
            return
    data, records, rows = materials(args.build, root, inputs, platform)
    cache_sha = digest(args.build / 'CMakeCache.txt')
    cache = read(args.build / 'CMakeCache.txt').decode('utf-8')
    configuration = [line for line in cache.splitlines() if line.startswith((
        'CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:',
        'SC_BUILD_MEDIA:', 'SC_BUILD_DESKTOP:', 'SC_BUILD_PIPEWIRE:'))]
    require(not args.output.exists(), 'Collection output already exists; preserve prior evidence')
    args.output.mkdir(parents=True)
    images = args.output / 'images'
    images.mkdir()
    for row in rows:
        shutil.copy2(args.build / row['path'], images / row['path'])
        require(digest(images / row['path']) == row['sha256'], 'Image changed during copy')
    proof = args.output / 'proof'
    proof.mkdir()
    for name in ['controller.stdout', 'controller.stderr', 'controller-process.json', 'controller-command.json'] + [r + '.identity.json' for r in ROLES]:
        (proof / name).write_bytes(read(records / name))
    (proof / 'positive-receipt.json').write_bytes(read(args.build / 'installed-restoration-controller-evidence/receipt.json'))
    (args.output / 'build-inputs.json').write_bytes(read(args.inputs))
    source = args.output / 'application-build-source.tar.gz'
    with tarfile.open(source, 'w:gz') as archive:
        for row in inputs['files']:
            archive.add(root / row['name'], arcname='soundcurrent-daw/' + row['name'], recursive=False)
    audit_source(source, inputs)
    require(source_snapshot(root, args.expected_source) == inputs and materials(args.build, root, inputs, platform)[2] == rows
            and digest(args.build / 'CMakeCache.txt') == cache_sha,
            'Source or native images changed during collection')
    profile = package_profile(rows, inputs, platform, build_type=types[0], cache_sha256=cache_sha,
                              positive_receipt_sha256=digest(proof / 'positive-receipt.json'))
    profile_path = args.output / 'package-profile.json'
    profile_bytes = (json.dumps(profile, indent=2) + '\n').encode('utf-8')
    profile_path.write_bytes(profile_bytes)
    require(read(profile_path) == profile_bytes, 'Package profile byte readback differs')
    result = dict(format='sc-restoration-worker-images-v1', sourceCommit=inputs['sourceCommit'], sourceTree=inputs['sourceTree'],
                  nativePlatform=platform, buildType=types[0], images=rows, sourceInputsUnchanged=True, repositorySourceArchived=True,
                  sourceArchive=dict(path=source.name, bytes=source.stat().st_size, sha256=digest(source)),
                  cmakeCacheSha256=cache_sha, buildConfiguration=configuration,
                  positiveReceiptSha256=digest(proof / 'positive-receipt.json'), nativeImageEvidenceOnly=True,
                  dependencySourceBundleQualified=False, standaloneBundleQualified=False, installerQualified=False,
                  installedGuiQualified=False, sourceExclusions=EXCLUDED,
                  packageProfile=dict(path=profile_path.name, bytes=profile_path.stat().st_size,
                                      sha256=digest(profile_path)))
    (args.output / 'collection.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'sourceCommit': inputs['sourceCommit'], 'nativeImages': 4, 'installerQualified': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--snapshot-inputs', type=Path)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--allow-absent', action='store_true')
    args = parser.parse_args()
    if args.snapshot_inputs:
        require(not any((args.build, args.inputs, args.output)) and not args.snapshot_inputs.exists(), 'Conflicting/existing snapshot output')
        args.snapshot_inputs.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot_inputs.write_text(json.dumps(source_snapshot(ROOT, args.expected_source), indent=2) + '\n', encoding='utf-8')
    else:
        require(all((args.build, args.inputs, args.output)), 'Build, inputs and output are required')
        collect(args)
