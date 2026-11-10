# SPDX-License-Identifier: GPL-3.0-only
"""Mutation tests for evidence collection; fixtures are not native qualification."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile
import unittest

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
            'CMAKE_HOME_DIRECTORY:INTERNAL=' + str(self.source) + '\n', encoding='utf-8')
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

    def test_collect_binds_four_images_and_exact_repository_source(self):
        tool.collect(self.args, self.source, 'linux')
        data = tool.read_json(self.args.output / 'collection.json')
        self.assertEqual(len(data['images']), 4)
        self.assertFalse(data['installerQualified'])
        self.assertFalse(data['dependencySourceBundleQualified'])
        tool.audit_source(self.args.output / data['sourceArchive']['path'], self.inputs)
        with tarfile.open(self.args.output / data['sourceArchive']['path']) as archive:
            self.assertIn('soundcurrent-daw/LICENSE', archive.getnames())
            self.assertNotIn('soundcurrent-daw/tests/results/history.txt', archive.getnames())

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
        tool.collect(self.args, self.source, 'linux')
        self.assertFalse(self.args.output.exists())
        self.args.expected_source = '0' * 40
        with self.assertRaises(RuntimeError):
            tool.collect(self.args, self.source, 'linux')


if __name__ == '__main__':
    unittest.main()
