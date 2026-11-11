# SPDX-License-Identifier: GPL-3.0-only
"""Mutation tests for evidence collection; fixtures are not native qualification."""
import argparse
import copy
import io
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('worker_images', ROOT / 'tools/restoration_worker_images.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


def write_json(path, data):
    path.write_text(json.dumps(data), encoding='utf-8')


def elf(tag):
    data = bytearray(64)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, 62)
    return bytes(data) + tag.encode()


class Images(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='native images é ')
        self.top = Path(self.temporary.name)
        self.source, self.build = self.top / 'source', self.top / 'build'
        self.source.mkdir()
        self.build.mkdir()
        for name in ['tools/asset_restoration_worker.cpp', 'tools/saved_restoration_worker.cpp',
                     'tests/controller_restoration_tests.cpp', 'LICENSE', 'CMakeLists.txt', 'tests/results/history.txt']:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(name + '\n', encoding='utf-8')
        for args in [('init', '-q', '--object-format=sha1'), ('config', 'user.name', 'Source Fixture'),
                     ('config', 'user.email', 'fixture@example.invalid'), ('add', '.'), ('commit', '-qm', 'fixture')]:
            subprocess.run(['git', *args], cwd=self.source, check=True, capture_output=True)
        self.head = tool.git(self.source, 'rev-parse', 'HEAD')
        self.inputs = tool.source_snapshot(self.source, self.head)
        self.input_path = self.top / 'inputs.json'
        write_json(self.input_path, self.inputs)
        (self.build / 'CMakeCache.txt').write_text(
            'CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.source) + '\nCMAKE_BUILD_TYPE:STRING=Debug\n', encoding='utf-8')
        driver = self.build / 'sc-controller-restoration-tests'
        driver.write_bytes(elf('driver'))
        self.receipt = self.build / 'installed-restoration-controller-evidence/receipt.json'
        self.records = self.receipt.parent / ('run-' + 'a' * 32)
        self.records.mkdir(parents=True)
        controller = dict(pid=123, exitCode=0, executableSha256=tool.digest(driver),
                          sourceSha256=tool.digest(self.source / 'tests/controller_restoration_tests.cpp'))
        write_json(self.records / 'controller-process.json', controller)
        write_json(self.records / 'controller-command.json', [str(driver), '--installed-workers', str(self.build / 'installed workers x/bin')])
        (self.records / 'controller.stdout').write_bytes(b'422 actual installed controller-owned restoration checks passed; four production roles; existing positive operation/PID/bytes/history guards\n')
        (self.records / 'controller.stderr').write_bytes(b'')
        rows = []
        for role, (name, source) in tool.ROLES.items():
            image = self.build / name
            image.write_bytes(elf(role))
            row = dict(role=role, executable=name, executableSha256=tool.digest(image),
                       installedSha256=tool.digest(image), source=source, sourceSha256=tool.digest(self.source / source))
            write_json(self.records / (role + '.identity.json'), row)
            rows.append(row)
        self.data = dict(component='restoration-qualification', positiveCases=tool.CASES, exactFileSet=True,
                         installedBytesMatch=True, installedControllerWorkflows=True, standaloneBundleQualified=False,
                         installedGuiQualified=False, runDirectory=self.records.name, controller=controller, checks=422, roles=rows)
        write_json(self.receipt, self.data)
        self.args = argparse.Namespace(inputs=self.input_path, expected_source=self.head, allow_absent=False,
                                       build=self.build, output=self.top / 'output')

    def tearDown(self):
        self.temporary.cleanup()

    def profile(self, rows, inputs, platform, **overrides):
        context = dict(build_type='Debug', cache_sha256=tool.digest(self.build / 'CMakeCache.txt'),
                       positive_receipt_sha256=tool.digest(self.receipt))
        context.update(overrides)
        return tool.package_profile(rows, inputs, platform, **context)

    def test_collect_binds_four_images_and_exact_repository_source(self):
        tool.collect(self.args, self.source, 'linux')
        data = tool.read_json(self.args.output / 'collection.json')
        self.assertEqual(len(data['images']), 4)
        self.assertEqual(data['buildType'], 'Debug')
        self.assertFalse(data['installerQualified'])
        self.assertFalse(data['dependencySourceBundleQualified'])
        profile_path = self.args.output / data['packageProfile']['path']
        profile = tool.read_json(profile_path)
        self.assertEqual(data['packageProfile']['sha256'], tool.digest(profile_path))
        self.assertEqual(profile, self.profile(data['images'], self.inputs, 'linux'))
        self.assertTrue(profile['helperOnly'])
        self.assertFalse(profile['defaultPreviewInventoryChanged'])
        tool.audit_source(self.args.output / data['sourceArchive']['path'], self.inputs)
        with tarfile.open(self.args.output / data['sourceArchive']['path']) as archive:
            self.assertIn('soundcurrent-daw/LICENSE', archive.getnames())
            self.assertNotIn('soundcurrent-daw/tests/results/history.txt', archive.getnames())

    def verify(self, **changes):
        context = dict(expected_source=self.head, platform='linux',
                       collection_sha256=changes.get('collection_sha256') or tool.digest(self.args.output / 'collection.json'))
        context.update(changes)
        return tool.verify_package_profile(self.args.output, **context)

    def rebind(self):
        manifest = self.args.output / 'collection.json'
        data = tool.read_json(manifest)
        data['fileBindings'] = [dict(path=p.relative_to(self.args.output).as_posix(),
                                     bytes=p.stat().st_size, sha256=tool.digest(p))
                                for p in sorted(self.args.output.rglob('*'))
                                if p.is_file() and p != manifest]
        write_json(manifest, data)

    def test_reader_pin_binds_every_proof_and_input_byte(self):
        tool.collect(self.args, self.source, 'linux')
        for name in ['proof/controller.stderr', 'proof/controller.stdout',
                     'proof/controller-command.json', 'build-inputs.json']:
            path = self.args.output / name
            old = path.read_bytes()
            if name.endswith('command.json'):
                changed = tool.decode_json(old)
                changed[2] = '/another/installation/bin'
                raw = json.dumps(changed).encode()
            elif name == 'build-inputs.json':
                raw = old + b' '
            else: raw = old + b'previously unbound detail\n'
            path.write_bytes(raw)
            with self.subTest(name=name), self.assertRaisesRegex(RuntimeError, 'manifest binding'):
                self.verify()
            path.write_bytes(old)

    def test_reader_refuses_legacy_missing_or_duplicate_bindings(self):
        tool.collect(self.args, self.source, 'linux')
        path = self.args.output / 'collection.json'
        original = tool.read_json(path)
        for case in ['legacy', 'missing', 'duplicate', 'unknown']:
            data = copy.deepcopy(original)
            if case == 'legacy': data['format'] = 'sc-restoration-worker-images-v1'
            elif case == 'missing': data['fileBindings'].pop()
            elif case == 'duplicate': data['fileBindings'][-1] = data['fileBindings'][0]
            else: data['fileBindings'][0]['unreviewed'] = True
            write_json(path, data)
            with self.subTest(case=case), self.assertRaises(RuntimeError): self.verify()

    def test_tar_budget_includes_pax_headers_padding_and_trailing_bytes(self):
        stream = io.BytesIO()
        payload = b'valid'
        info = tarfile.TarInfo('soundcurrent-daw/a')
        info.size = len(payload)
        info.pax_headers = {'comment': 'x' * (70 * 1024)}
        with tarfile.open(fileobj=stream, mode='w:gz') as tar:
            tar.addfile(info, io.BytesIO(payload))
        inputs = dict(files=[dict(name='a', sha256=hashlib.sha256(payload).hexdigest())])
        archive = self.top / 'pax.tar.gz'
        archive.write_bytes(stream.getvalue())
        with mock.patch.object(tool, 'SOURCE_TAR_LIMIT', 64 * 1024):
            with self.assertRaisesRegex(RuntimeError, 'decompressed source tar'):
                tool.audit_source(archive, inputs)
        # A normal archive with expanded trailing bytes must use the same budget.
        plain = io.BytesIO()
        info.pax_headers = {}
        with tarfile.open(fileobj=plain, mode='w:') as tar:
            tar.addfile(info, io.BytesIO(payload))
        archive.write_bytes(gzip.compress(plain.getvalue() + b'0' * (70 * 1024)))
        with mock.patch.object(tool, 'SOURCE_TAR_LIMIT', 64 * 1024):
            with self.assertRaisesRegex(RuntimeError, 'decompressed source tar'):
                tool.audit_source(archive, inputs)

    def test_profile_reader_is_read_only_and_requires_caller_context(self):
        tool.collect(self.args, self.source, 'linux')
        before = {p.relative_to(self.args.output): p.read_bytes()
                  for p in self.args.output.rglob('*') if p.is_file()}
        result = self.verify()
        self.assertEqual(len(result['payload']), 4)
        self.assertFalse(result['nativeExecutionPerformed'])
        self.assertFalse(result['packageAdmissionGranted'])
        self.assertEqual(before, {p.relative_to(self.args.output): p.read_bytes()
                                 for p in self.args.output.rglob('*') if p.is_file()})
        for context in [dict(collection_sha256='0' * 64), dict(expected_source='0' * 40),
                        dict(platform='win32'), dict(platform='darwin')]:
            with self.subTest(context=context), self.assertRaises(RuntimeError):
                self.verify(**context)

    def test_profile_reader_refuses_rehashed_profile_schema_and_layout_changes(self):
        tool.collect(self.args, self.source, 'linux')
        profile_path = self.args.output / 'package-profile.json'
        manifest_path = self.args.output / 'collection.json'
        original = profile_path.read_bytes()
        manifest = tool.read_json(manifest_path)
        for mutation in ['path', 'authority', 'unknown', 'duplicate']:
            profile = tool.decode_json(original)
            if mutation == 'path': profile['payload'][0]['payloadPath'] = '../worker'
            elif mutation == 'authority': profile['installerQualified'] = True
            elif mutation == 'unknown': profile['extra'] = True
            if mutation == 'duplicate':
                raw = original.replace(b'{', b'{"helperOnly":true,', 1)
            else: raw = (json.dumps(profile, indent=2) + '\n').encode()
            profile_path.write_bytes(raw)
            manifest['packageProfile'].update(bytes=len(raw), sha256=tool.digest(profile_path))
            write_json(manifest_path, manifest)
            self.rebind()
            # Supply the changed pin to exercise schema validation beyond hash refusal.
            # Such a self-derived pin does not independently qualify these fixtures.
            with self.subTest(mutation=mutation), self.assertRaises(RuntimeError):
                self.verify()

    def test_profile_reader_refuses_broken_inventory_image_and_proof(self):
        tool.collect(self.args, self.source, 'linux')
        for name in ['images/sc-saved-manifest-worker', 'proof/manifest.identity.json',
                     'proof/controller-process.json', 'application-build-source.tar.gz']:
            path = self.args.output / name
            old = path.read_bytes()
            path.write_bytes(b'changed')
            with self.subTest(name=name), self.assertRaises((RuntimeError, ValueError)):
                self.verify()
            path.write_bytes(old)
        extra = self.args.output / 'unexpected'
        extra.write_bytes(b'extra')
        with self.assertRaises(RuntimeError): self.verify()
        extra.unlink()
        path = self.args.output / 'proof/controller.stderr'
        path.rename(path.with_suffix('.saved'))
        with self.assertRaises(RuntimeError): self.verify()
        path.with_suffix('.saved').rename(path)
        with mock.patch.object(tool, 'linked', side_effect=lambda p: p == path):
            with self.assertRaises(RuntimeError): self.verify()

    def test_collection_is_published_only_after_reader_success(self):
        with mock.patch.object(tool, 'verify_package_profile', side_effect=RuntimeError('reader refusal')):
            with self.assertRaisesRegex(RuntimeError, 'reader refusal'):
                tool.collect(self.args, self.source, 'linux')
        self.assertFalse((self.args.output / 'collection.json').exists())
        self.assertTrue((self.args.output / 'collection.pending.json').is_file())
        with self.assertRaises(RuntimeError): self.verify(collection_sha256='0' * 64)

    def test_package_profile_requires_exact_platform_roles_and_source_binding(self):
        rows = tool.materials(self.build, self.source, self.inputs, 'linux')[2]
        linux = self.profile(rows, self.inputs, 'linux')
        self.assertTrue(all(row['payloadPath'] == 'usr/bin/' + row['path'] for row in linux['payload']))
        # Contract-only PE rows, not a claim of native Windows execution.
        windows = [dict(row, path=row['path'] + '.exe', nativeIdentity='PE-amd64') for row in rows]
        profile = self.profile(windows, self.inputs, 'win32')
        self.assertTrue(all(row['payloadPath'] == row['path'] for row in profile['payload']))
        for role_rows, platform in [(rows, 'win32'), (windows, 'linux'), (rows, 'darwin'),
                                    (rows[:-1], 'linux'), (rows + [rows[0]], 'linux')]:
            with self.assertRaises(RuntimeError):
                self.profile(role_rows, self.inputs, platform)
        for field, value in [('path', '../sc-asset-restoration-worker'), ('sourceSha256', '0' * 64),
                             ('source', 'foreign.cpp'), ('sha256', 'bad'), ('bytes', True),
                             ('bytes', 256 * 1024 * 1024 + 1), ('nativeIdentity', 'PE-amd64')]:
            changed = copy.deepcopy(rows)
            changed[0][field] = value
            with self.assertRaises(RuntimeError):
                self.profile(changed, self.inputs, 'linux')
        for change in ['extra', 'missing', 'aggregate']:
            changed = copy.deepcopy(rows)
            if change == 'extra': changed[0]['unreviewed'] = True
            elif change == 'missing': del changed[0]['role']
            else:
                for row in changed: row['bytes'] = 200 * 1024 * 1024
            with self.assertRaises(RuntimeError):
                self.profile(changed, self.inputs, 'linux')
        for context in [dict(build_type='unknown'), dict(cache_sha256='bad'),
                        dict(positive_receipt_sha256='bad')]:
            with self.assertRaises(RuntimeError):
                self.profile(rows, self.inputs, 'linux', **context)
        for inputs in [{}, dict(self.inputs, files=[{}])]:
            with self.assertRaises(RuntimeError):
                self.profile(rows, inputs, 'linux')
        changed = copy.deepcopy(rows)
        changed[1]['sha256'] = changed[0]['sha256']
        with self.assertRaises(RuntimeError):
            self.profile(changed, self.inputs, 'linux')
        inputs = copy.deepcopy(self.inputs)
        inputs['files'].append(inputs['files'][0])
        with self.assertRaises(RuntimeError):
            self.profile(rows, inputs, 'linux')

    def test_refuse_image_changed_after_positive_execution(self):
        (self.build / 'sc-saved-manifest-worker').write_bytes(elf('replacement'))
        with self.assertRaises(RuntimeError):
            tool.materials(self.build, self.source, self.inputs, 'linux')

    def test_refuse_role_source_or_pid_mutation(self):
        for field, value in [('sourceSha256', '0' * 64), ('pid', 0), ('exitCode', True)]:
            original = self.data['controller'][field]
            self.data['controller'][field] = value
            write_json(self.receipt, self.data)
            with self.assertRaises(RuntimeError):
                tool.materials(self.build, self.source, self.inputs, 'linux')
            self.data['controller'][field] = original

    def test_refuse_missing_extra_or_aliased_role(self):
        for rows in [self.data['roles'][:-1], self.data['roles'] + self.data['roles'][:1], self.data['roles'][:3] + self.data['roles'][:1]]:
            data = dict(self.data, roles=rows)
            write_json(self.receipt, data)
            with self.assertRaises(RuntimeError):
                tool.materials(self.build, self.source, self.inputs, 'linux')

    def test_refuse_dirty_source_and_wrong_commit(self):
        with self.assertRaises(RuntimeError):
            tool.source_snapshot(self.source, '0' * 40)
        (self.source / 'LICENSE').write_text('changed')
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')

    def test_refuse_existing_output_without_overwrite(self):
        self.args.output.mkdir()
        marker = self.args.output / 'prior-evidence'
        marker.write_bytes(b'preserve')
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')
        self.assertEqual(marker.read_bytes(), b'preserve')

    def test_refuse_duplicate_json_keys_and_false_completion(self):
        self.receipt.write_text('{"roles":[],"roles":[]}')
        with self.assertRaises(RuntimeError):
            tool.read_json(self.receipt)
        write_json(self.receipt, self.data)
        (self.records / 'controller.stdout').write_bytes(b'422 actual controller-owned restoration checks passed\n')
        with self.assertRaises(RuntimeError):
            tool.materials(self.build, self.source, self.inputs, 'linux')

    def test_native_header_refusals(self):
        image = self.top / 'native.exe'
        data = bytearray(256)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 60, 128)
        data[128:132] = b'PE\0\0'
        struct.pack_into('<H', data, 132, 0x8664)
        image.write_bytes(data)
        self.assertEqual(tool.native_identity(image, 'win32'), 'PE-amd64')
        for platform in ['linux', 'darwin']:
            with self.assertRaises(RuntimeError):
                tool.native_identity(image, platform)
        struct.pack_into('<I', data, 60, 4096)
        image.write_bytes(data)
        with self.assertRaises(RuntimeError):
            tool.native_identity(image, 'win32')

    def test_absent_shard_never_creates_collection(self):
        self.receipt.unlink()
        self.args.allow_absent = True
        plan = dict(shards=[dict(names=['another-test']), dict(names=['installed-restoration-controller'])],
                    shard=0, allTestsAssignedExactlyOnce=True, exitCode=0, inventoryCount=2)
        write_json(self.build / 'ci-shard-plan.json', plan)
        tool.collect(self.args, self.source, 'linux')
        self.assertFalse(self.args.output.exists())
        plan['shard'] = 1
        write_json(self.build / 'ci-shard-plan.json', plan)
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')
        plan['shards'][1]['names'] = ['renamed-positive-test']
        write_json(self.build / 'ci-shard-plan.json', plan)
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')
        (self.build / 'CMakeCache.txt').unlink()
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')
        self.args.expected_source = '0' * 40
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')


if __name__ == '__main__':
    unittest.main()
