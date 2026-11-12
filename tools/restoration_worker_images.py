#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect native worker images and repository source; never qualify an installer."""
import argparse
import hashlib
import gzip
import io
import json
import os
import stat
from pathlib import Path, PurePosixPath, PureWindowsPath
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


def digest(path, maximum=None):
    with path.open('rb') as stream:
        if maximum is None:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
        result = hashlib.sha256(); size = 0
        while True:
            data = stream.read(min(65536, maximum - size + 1))
            if not data: break
            size += len(data)
            require(size <= maximum, 'Image grew beyond bound')
            result.update(data)
        return result.hexdigest()


def read(path, maximum=2 * 1024 * 1024):
    require(path.is_file() and not linked(path) and path.stat().st_size <= maximum, 'Missing/linked/oversized evidence: ' + path.name)
    with path.open('rb') as stream:
        data = stream.read(maximum + 1)
    require(len(data) <= maximum, 'Evidence grew beyond bound')
    return data


def decode_json(data):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'Duplicate evidence key')
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def read_json(path):
    return decode_json(read(path))


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


SOURCE_TAR_LIMIT = 256 * 1024 * 1024


class BoundedSourceStream:
    """Count every decompressed tar byte, including extended headers and padding."""
    def __init__(self, stream):
        self.stream = stream
        self.remaining = SOURCE_TAR_LIMIT

    def read(self, size=-1):
        amount = self.remaining + 1 if size < 0 else min(size, self.remaining + 1)
        data = self.stream.read(amount)
        require(len(data) <= self.remaining, 'Excess decompressed source tar bytes')
        self.remaining -= len(data)
        return data


def audit_source(path, inputs, *, source_bytes=None):
    expected = {'soundcurrent-daw/' + row['name']: row['sha256'] for row in inputs['files']}
    require(len(expected) == len(inputs['files']) <= 100000, 'Duplicate/excess source archive inputs')
    seen = set(); total = 0
    compressed = path.open('rb') if source_bytes is None else io.BytesIO(source_bytes)
    with compressed, gzip.GzipFile(fileobj=compressed) as expanded:
        bounded = BoundedSourceStream(expanded)
        with tarfile.open(fileobj=bounded, mode='r|') as archive:
            for member in archive:
                require(member.isfile() and member.name in expected and member.name not in seen, 'Unexpected source archive entry')
                total += member.size
                require(0 <= member.size <= 64 * 1024 * 1024 and total <= SOURCE_TAR_LIMIT, 'Excess source archive bytes')
                seen.add(member.name)
                with archive.extractfile(member) as stream:
                    require(hashlib.file_digest(stream, 'sha256').hexdigest() == expected[member.name], 'Source archive bytes differ')
        # Include trailing decompressed bytes even after the tar end marker.
        while bounded.read(65536):
            pass
    require(seen == set(expected), 'Source archive inputs missing')


def package_profile(rows, inputs, platform, *, build_type, cache_sha256, positive_receipt_sha256):
    """Bind an explicit helper-only payload plan, without granting package admission."""
    require(platform in ('linux', 'win32'), 'Unsupported package profile platform')
    require(build_type in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')
            and all(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value)
                    for value in (cache_sha256, positive_receipt_sha256)), 'Invalid package build context')
    require(isinstance(inputs, dict) and isinstance(inputs.get('files'), list)
            and all(isinstance(row, dict) and set(row) == {'name', 'sha256'}
                    and isinstance(row['name'], str) and row['name'] != ''
                    and not PurePosixPath(row['name']).is_absolute()
                    and '..' not in PurePosixPath(row['name']).parts and '\\' not in row['name']
                    and PurePosixPath(row['name']).as_posix() == row['name']
                    and isinstance(row['sha256'], str)
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


def verify_package_profile(directory, *, expected_source, platform, collection_sha256,
                           _manifest_name='collection.json'):
    """Read-only verification against a caller's qualified pin, never package admission."""
    require(platform in ('linux', 'win32') and isinstance(expected_source, str)
            and re.fullmatch('[0-9a-f]{40}', expected_source) and isinstance(collection_sha256, str)
            and re.fullmatch('[0-9a-f]{64}', collection_sha256), 'Invalid requested collection context')
    require(_manifest_name in ('collection.json', 'collection.pending.json'), 'Invalid manifest selection')
    require(directory.is_dir() and not linked(directory), 'Invalid collection directory')
    names = set()
    for index, path in enumerate(directory.rglob('*')):
        require(index < 32 and not linked(path), 'Linked/excess collection entry')
        relative = path.relative_to(directory).as_posix()
        if path.is_dir():
            require(relative in ('images', 'proof'), 'Unexpected collection directory')
        else:
            require(path.is_file(), 'Non-plain collection entry')
            names.add(relative)
    suffix = '.exe' if platform == 'win32' else ''
    proof_names = ['controller.stdout', 'controller.stderr', 'controller-process.json', 'controller-command.json']
    expected = {_manifest_name, 'package-profile.json', 'build-inputs.json', 'application-build-source.tar.gz',
                'proof/positive-receipt.json'} | {'images/' + name + suffix for name, _ in ROLES.values()}
    expected |= {'proof/' + name for name in proof_names} | {'proof/' + role + '.identity.json' for role in ROLES}
    require(names == expected, 'Collection file set differs')
    manifest_path = directory / _manifest_name
    raw_manifest = read(manifest_path)
    require(hashlib.sha256(raw_manifest).hexdigest() == collection_sha256, 'Collection differs from caller pin')
    m = decode_json(raw_manifest)
    require(isinstance(m, dict) and set(m) == {'format', 'sourceCommit', 'sourceTree', 'nativePlatform', 'buildType', 'images',
                      'sourceInputsUnchanged', 'repositorySourceArchived', 'sourceArchive', 'cmakeCacheSha256',
                      'buildConfiguration', 'positiveReceiptSha256', 'nativeImageEvidenceOnly',
                      'dependencySourceBundleQualified', 'standaloneBundleQualified', 'installerQualified',
                      'installedGuiQualified', 'sourceExclusions', 'packageProfile', 'fileBindings'}, 'Unknown collection schema')
    require(m['format'] == 'sc-restoration-worker-images-v2' and m['sourceCommit'] == expected_source
            and m['nativePlatform'] == platform and m['sourceInputsUnchanged'] is True
            and m['repositorySourceArchived'] is True and m['nativeImageEvidenceOnly'] is True
            and m['sourceExclusions'] == EXCLUDED, 'Collection context differs')
    for flag in ('dependencySourceBundleQualified', 'standaloneBundleQualified', 'installerQualified', 'installedGuiQualified'):
        require(m[flag] is False, 'Collection grants unsupported qualification')
    configuration = m['buildConfiguration']
    require(isinstance(configuration, list) and configuration
            and all(isinstance(line, str) and line.startswith(('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:',
                    'SC_BUILD_MEDIA:', 'SC_BUILD_DESKTOP:', 'SC_BUILD_PIPEWIRE:')) for line in configuration)
            and [line for line in configuration if line.startswith('CMAKE_BUILD_TYPE:')]
                == ['CMAKE_BUILD_TYPE:STRING=' + m['buildType']], 'Build configuration differs')
    bindings = m['fileBindings']
    require(isinstance(bindings, list) and len(bindings) == len(expected) - 1
            and all(isinstance(row, dict) and set(row) == {'path', 'bytes', 'sha256'}
                    and isinstance(row['path'], str) and type(row['bytes']) is int
                    and row['bytes'] >= 0 and isinstance(row['sha256'], str)
                    and re.fullmatch('[0-9a-f]{64}', row['sha256']) for row in bindings), 'Invalid inventory bindings')
    bound = {row['path']: row for row in bindings}
    require(len(bound) == len(bindings) and set(bound) == expected - {_manifest_name}, 'Unbound collection files')

    def bound_read(name, maximum=2 * 1024 * 1024):
        raw = read(directory / name, maximum)
        require(len(raw) == bound[name]['bytes'] and hashlib.sha256(raw).hexdigest() == bound[name]['sha256'],
                'Collection file differs from manifest binding: ' + name)
        return raw

    inputs = decode_json(bound_read('build-inputs.json'))
    require(isinstance(inputs, dict) and inputs.get('sourceCommit') == expected_source and inputs.get('sourceTree') == m['sourceTree']
            and inputs['excluded'] == EXCLUDED, 'Captured source identity differs')
    expected_profile = package_profile(m['images'], inputs, platform, build_type=m['buildType'],
                                      cache_sha256=m['cmakeCacheSha256'], positive_receipt_sha256=m['positiveReceiptSha256'])
    pointer = m['packageProfile']; raw_profile = bound_read('package-profile.json')
    require(isinstance(pointer, dict) and set(pointer) == {'path', 'bytes', 'sha256'} and pointer['path'] == 'package-profile.json'
            and type(pointer['bytes']) is int and len(raw_profile) == pointer['bytes']
            and hashlib.sha256(raw_profile).hexdigest() == pointer['sha256'], 'Profile bytes differ')
    require(raw_profile == (json.dumps(expected_profile, indent=2) + '\n').encode('utf-8')
            and decode_json(raw_profile) == expected_profile, 'Profile schema/layout/context differs')
    pins = {row['name']: row['sha256'] for row in inputs['files']}
    raw_positive = bound_read('proof/positive-receipt.json')
    positive = decode_json(raw_positive)
    require(isinstance(positive, dict) and hashlib.sha256(raw_positive).hexdigest() == m['positiveReceiptSha256']
            and positive['component'] == 'restoration-qualification' and positive['positiveCases'] == CASES
            and positive['exactFileSet'] is True and positive['installedBytesMatch'] is True
            and positive['installedControllerWorkflows'] is True and positive['standaloneBundleQualified'] is False
            and positive['installedGuiQualified'] is False and len(positive['roles']) == 4
            and {row['role'] for row in positive['roles']} == set(ROLES), 'Positive evidence differs')
    driver = positive['controller']
    require(decode_json(bound_read('proof/controller-process.json')) == driver
            and driver['sourceSha256'] == pins['tests/controller_restoration_tests.cpp']
            and type(driver['pid']) is int and driver['pid'] > 0
            and type(driver['exitCode']) is int and driver['exitCode'] == 0, 'Controller process evidence differs')
    command = decode_json(bound_read('proof/controller-command.json'))
    require(isinstance(command, list) and len(command) == 3 and all(isinstance(v, str) for v in command)
            and command[1] == '--installed-workers'
            and any(kind(command[0]).is_absolute() and kind(command[0]).name == 'sc-controller-restoration-tests' + suffix
                    and kind(command[2]).is_absolute() and kind(command[2]).name == 'bin'
                    for kind in (PurePosixPath, PureWindowsPath)), 'Controller command evidence differs')
    output = bound_read('proof/controller.stdout', 1024 * 1024)
    bound_read('proof/controller.stderr', 1024 * 1024)
    matches = re.findall(rb'(?m)^(\d+) actual installed controller-owned restoration checks passed; four production roles; existing positive operation/PID/bytes/history guards\r?$', output)
    require(len(matches) == 1 and type(positive['checks']) is int and int(matches[0]) == positive['checks'] > 0,
            'Positive completion evidence differs')
    for row in m['images']:
        image = directory / 'images' / row['path']
        original = next(p for p in positive['roles'] if p['role'] == row['role'])
        require(image.stat().st_size == row['bytes'] and digest(image, row['bytes']) == row['sha256']
                and bound['images/' + row['path']]['bytes'] == row['bytes']
                and bound['images/' + row['path']]['sha256'] == row['sha256']
                and native_identity(image, platform) == row['nativeIdentity']
                and original['executable'] == row['path'] and original['source'] == row['source']
                and original['sourceSha256'] == row['sourceSha256']
                and original['executableSha256'] == original['installedSha256'] == row['sha256']
                and decode_json(bound_read('proof/' + row['role'] + '.identity.json')) == original, 'Image/proof differs')
    source = m['sourceArchive']; archive = directory / 'application-build-source.tar.gz'
    require(isinstance(source, dict) and set(source) == {'path', 'bytes', 'sha256'} and source['path'] == archive.name
            and type(source['bytes']) is int and 0 < source['bytes'] <= 128 * 1024 * 1024
            and archive.stat().st_size == source['bytes'], 'Source archive differs')
    raw_source = bound_read(archive.name, 128 * 1024 * 1024)
    require(len(raw_source) == source['bytes'] and hashlib.sha256(raw_source).hexdigest() == source['sha256'], 'Source archive hash differs')
    audit_source(archive, inputs, source_bytes=raw_source)
    return dict(format='sc-restoration-package-profile-verification-v1', sourceCommit=expected_source,
                nativePlatform=platform, collectionSha256=collection_sha256, profileSha256=pointer['sha256'],
                payload=expected_profile['payload'], nativeExecutionPerformed=False, packageAdmissionGranted=False,
                standaloneBundleQualified=False, installerQualified=False, installedGuiQualified=False)


def copy_bound(source, destination, size, sha256, *, executable=False):
    """Copy exact pinned bytes without overwriting, then independently read back."""
    require(source.is_file() and not linked(source) and source.stat().st_size == size, 'Copy source differs')
    destination.parent.mkdir(parents=True, exist_ok=True)
    copied = 0; digest_value = hashlib.sha256()
    with source.open('rb') as incoming, destination.open('xb') as outgoing:
        while True:
            chunk = incoming.read(65536)
            if not chunk: break
            copied += len(chunk)
            require(copied <= size, 'Copy source grew beyond pinned extent')
            digest_value.update(chunk); outgoing.write(chunk)
    require(copied == size and digest_value.hexdigest() == sha256, 'Copied bytes differ from pin')
    destination.chmod(0o755 if executable else 0o644)
    require(destination.stat().st_size == size and digest(destination, size) == sha256, 'Copied byte readback differs')


def stage_package_profile(directory, output, *, expected_source, platform, collection_sha256):
    """Stage candidate bytes and complete provenance; no package admission or execution."""
    verified = verify_package_profile(directory, expected_source=expected_source, platform=platform,
                                      collection_sha256=collection_sha256)
    require(not output.exists() and not linked(output), 'Candidate output exists; preserve prior evidence')
    require(output.parent.is_dir() and not linked(output.parent)
            and not output.resolve().is_relative_to(directory.resolve()), 'Invalid/nested candidate output')
    raw_manifest = read(directory / 'collection.json')
    require(hashlib.sha256(raw_manifest).hexdigest() == collection_sha256, 'Collection changed before copy')
    manifest = decode_json(raw_manifest)
    output.mkdir(mode=0o700)
    pending = output / 'candidate.pending.json'
    pending.write_text(json.dumps(dict(format='sc-restoration-payload-pending-v1', sourceCommit=expected_source)) + '\n', encoding='utf-8')
    provenance = output / 'provenance'
    provenance.mkdir()
    copy_bound(directory / 'collection.json', provenance / 'collection.json', len(raw_manifest), collection_sha256)
    for row in manifest['fileBindings']:
        copy_bound(directory / row['path'], provenance / row['path'], row['bytes'], row['sha256'])
    payload = []
    for row in verified['payload']:
        path = 'payload/' + row['payloadPath']
        copy_bound(provenance / 'images' / row['path'], output / path,
                   row['bytes'], row['sha256'], executable=platform == 'linux')
        payload.append(dict(row, stagedPath=path))
    copied = verify_package_profile(provenance, expected_source=expected_source, platform=platform,
                                    collection_sha256=collection_sha256)
    require(copied == verified, 'Copied collection verification differs')
    for row in payload:
        path = output / row['stagedPath']
        require(path.stat().st_size == row['bytes'] and digest(path, row['bytes']) == row['sha256'],
                'Final candidate payload readback differs')
    expected = {'candidate.pending.json', 'provenance/collection.json'}
    expected |= {'provenance/' + row['path'] for row in manifest['fileBindings']}
    expected |= {row['stagedPath'] for row in payload}
    directories = {'provenance', 'provenance/images', 'provenance/proof', 'payload'}
    if platform == 'linux': directories |= {'payload/usr', 'payload/usr/bin'}
    files = set()
    for count, path in enumerate(output.rglob('*')):
        require(count < 64 and not linked(path), 'Linked/excess staged entry')
        name = path.relative_to(output).as_posix()
        if path.is_dir(): require(name in directories, 'Unexpected staged directory')
        else:
            require(path.is_file(), 'Non-plain staged entry'); files.add(name)
    require(files == expected, 'Candidate staged file set differs')
    result = dict(format='sc-restoration-worker-payload-stage-v1', sourceCommit=expected_source,
                  nativePlatform=platform, collectionSha256=collection_sha256,
                  profileSha256=verified['profileSha256'], copiedCollection='provenance',
                  copiedCollectionVerified=True, payload=payload, payloadBytesReadbackVerified=True,
                  nativeExecutionPerformed=False, packageAdmissionGranted=False,
                  defaultPreviewInventoryChanged=False, dependencySourceBundleQualified=False,
                  standaloneBundleQualified=False, installerQualified=False, installedGuiQualified=False)
    raw_result = (json.dumps(result, indent=2) + '\n').encode('utf-8')
    pending.write_bytes(raw_result)
    require(read(pending) == raw_result, 'Candidate receipt byte readback differs')
    pending.rename(output / 'candidate.json')
    return result



def verify_staged_payload(directory, *, expected_source, platform, collection_sha256, candidate_sha256):
    """Verify staged bytes against two caller pins; never execute or admit a package."""
    require(isinstance(candidate_sha256, str) and re.fullmatch('[0-9a-f]{64}', candidate_sha256),
            'Invalid candidate receipt pin')
    require(directory.is_dir() and not linked(directory), 'Invalid candidate directory')
    raw = read(directory / 'candidate.json')
    require(hashlib.sha256(raw).hexdigest() == candidate_sha256, 'Candidate differs from caller pin')
    data = decode_json(raw)
    verified = verify_package_profile(directory / 'provenance', expected_source=expected_source,
                                      platform=platform, collection_sha256=collection_sha256)
    payload = [dict(row, stagedPath='payload/' + row['payloadPath']) for row in verified['payload']]
    expected_receipt = dict(format='sc-restoration-worker-payload-stage-v1', sourceCommit=expected_source,
                  nativePlatform=platform, collectionSha256=collection_sha256,
                  profileSha256=verified['profileSha256'], copiedCollection='provenance',
                  copiedCollectionVerified=True, payload=payload, payloadBytesReadbackVerified=True,
                  nativeExecutionPerformed=False, packageAdmissionGranted=False,
                  defaultPreviewInventoryChanged=False, dependencySourceBundleQualified=False,
                  standaloneBundleQualified=False, installerQualified=False, installedGuiQualified=False)
    # JSON comparison preserves bool/int distinctions, unlike Python dictionary equality.
    require(json.dumps(data, sort_keys=True) == json.dumps(expected_receipt, sort_keys=True),
            'Candidate receipt schema/context differs')
    manifest = read_json(directory / 'provenance/collection.json')
    expected = {'candidate.json', 'provenance/collection.json'}
    expected |= {'provenance/' + row['path'] for row in manifest['fileBindings']}
    expected |= {row['stagedPath'] for row in payload}
    directories = {'provenance', 'provenance/images', 'provenance/proof', 'payload'}
    if platform == 'linux': directories |= {'payload/usr', 'payload/usr/bin'}
    files = set()
    for count, path in enumerate(directory.rglob('*')):
        require(count < 64 and not linked(path), 'Linked/excess candidate entry')
        relative = path.relative_to(directory).as_posix()
        if path.is_dir(): require(relative in directories, 'Unexpected candidate directory')
        else:
            require(path.is_file(), 'Non-plain candidate entry'); files.add(relative)
    require(files == expected, 'Candidate file set differs')
    for row in payload:
        path = directory / row['stagedPath']
        require(path.stat().st_size == row['bytes'] and digest(path, row['bytes']) == row['sha256'],
                'Candidate payload differs')
        if platform == 'linux' and os.name != 'nt':
            require(stat.S_IMODE(path.stat().st_mode) == 0o755, 'Candidate executable mode differs')
    require(read(directory / 'candidate.json') == raw, 'Candidate receipt changed during verification')
    return dict(format='sc-restoration-staged-payload-verification-v1', sourceCommit=expected_source,
                nativePlatform=platform, collectionSha256=collection_sha256, candidateSha256=candidate_sha256,
                payload=payload, provenanceVerified=True, nativeExecutionPerformed=False,
                packageAdmissionGranted=False, dependencySourceBundleQualified=False,
                standaloneBundleQualified=False, installerQualified=False, installedGuiQualified=False)


def collect(args, root=ROOT, platform=sys.platform):
    require(getattr(args, 'dependency_output', None) is not None or getattr(args, 'dependency_tool', None) is None,
            'Dependency tool requires report output')
    require(getattr(args, 'dependency_output', None) is None or getattr(args, 'stage_output', None) is not None,
            'Dependency reports require staged output')
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
    result = dict(format='sc-restoration-worker-images-v2', sourceCommit=inputs['sourceCommit'], sourceTree=inputs['sourceTree'],
                  nativePlatform=platform, buildType=types[0], images=rows, sourceInputsUnchanged=True, repositorySourceArchived=True,
                  sourceArchive=dict(path=source.name, bytes=source.stat().st_size, sha256=digest(source)),
                  cmakeCacheSha256=cache_sha, buildConfiguration=configuration,
                  positiveReceiptSha256=digest(proof / 'positive-receipt.json'), nativeImageEvidenceOnly=True,
                  dependencySourceBundleQualified=False, standaloneBundleQualified=False, installerQualified=False,
                  installedGuiQualified=False, sourceExclusions=EXCLUDED,
                  packageProfile=dict(path=profile_path.name, bytes=profile_path.stat().st_size,
                                      sha256=digest(profile_path)))
    result['fileBindings'] = [dict(path=path.relative_to(args.output).as_posix(), bytes=path.stat().st_size, sha256=digest(path))
                              for path in sorted(args.output.rglob('*')) if path.is_file()]
    pending = args.output / 'collection.pending.json'
    pending.write_bytes((json.dumps(result, indent=2) + '\n').encode('utf-8'))
    verify_package_profile(args.output, expected_source=args.expected_source, platform=platform,
                           collection_sha256=digest(pending), _manifest_name=pending.name)
    pending.rename(args.output / 'collection.json')
    if getattr(args, 'stage_output', None) is not None:
        stage_package_profile(args.output, args.stage_output, expected_source=args.expected_source,
                              platform=platform, collection_sha256=digest(args.output / 'collection.json'))
        # Producer consistency check only; qualification still requires independently admitted pins.
        verify_staged_payload(args.stage_output, expected_source=args.expected_source, platform=platform,
                              collection_sha256=digest(args.output / 'collection.json'),
                              candidate_sha256=digest(args.stage_output / 'candidate.json'))
        if getattr(args, 'dependency_output', None) is not None:
            from restoration_dependency_reports import capture_dependencies
            capture_dependencies(args.stage_output, args.dependency_output, expected_source=args.expected_source,
                                 platform=platform, collection_sha256=digest(args.output / 'collection.json'),
                                 candidate_sha256=digest(args.stage_output / 'candidate.json'),
                                 inspector=getattr(args, 'dependency_tool', None))
    print(json.dumps({'sourceCommit': inputs['sourceCommit'], 'nativeImages': 4,
                      'profileReadbackVerified': True, 'installerQualified': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--snapshot-inputs', type=Path)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--allow-absent', action='store_true')
    parser.add_argument('--verify-profile', type=Path)
    parser.add_argument('--verify-staged', type=Path)
    parser.add_argument('--candidate-sha256')
    parser.add_argument('--collection-sha256')
    parser.add_argument('--stage-output', type=Path)
    parser.add_argument('--dependency-output', type=Path)
    parser.add_argument('--dependency-tool', type=Path)
    args = parser.parse_args()
    require(args.verify_profile is not None or args.verify_staged is not None or args.collection_sha256 is None,
            'Collection pin requires verification mode')
    require(args.verify_staged is not None or args.candidate_sha256 is None, 'Candidate pin requires staged mode')
    require(args.dependency_output is not None or args.dependency_tool is None, 'Dependency tool requires report output')
    require(args.dependency_output is None or args.stage_output is not None, 'Dependency reports require staged output')
    if args.verify_staged:
        require(not any((args.snapshot_inputs, args.build, args.inputs, args.output, args.allow_absent,
                         args.verify_profile, args.stage_output, args.dependency_output, args.dependency_tool))
                and args.collection_sha256 is not None and args.candidate_sha256 is not None,
                'Conflicting/incomplete staged verification inputs')
        print(json.dumps(verify_staged_payload(args.verify_staged, expected_source=args.expected_source,
                         platform=sys.platform, collection_sha256=args.collection_sha256,
                         candidate_sha256=args.candidate_sha256)))
    elif args.verify_profile:
        require(not any((args.snapshot_inputs, args.build, args.inputs, args.output, args.allow_absent, args.dependency_output, args.dependency_tool))
                and args.collection_sha256 is not None, 'Conflicting/incomplete profile verification inputs')
        if args.stage_output is not None:
            print(json.dumps(stage_package_profile(args.verify_profile, args.stage_output,
                             expected_source=args.expected_source, platform=sys.platform,
                             collection_sha256=args.collection_sha256)))
        else:
            print(json.dumps(verify_package_profile(args.verify_profile, expected_source=args.expected_source,
                                                   platform=sys.platform, collection_sha256=args.collection_sha256)))
    elif args.snapshot_inputs:
        require(not any((args.build, args.inputs, args.output, args.stage_output, args.dependency_output, args.dependency_tool)) and not args.snapshot_inputs.exists(), 'Conflicting/existing snapshot output')
        args.snapshot_inputs.parent.mkdir(parents=True, exist_ok=True)
        args.snapshot_inputs.write_text(json.dumps(source_snapshot(ROOT, args.expected_source), indent=2) + '\n', encoding='utf-8')
    else:
        require(all((args.build, args.inputs, args.output)), 'Build, inputs and output are required')
        collect(args)
