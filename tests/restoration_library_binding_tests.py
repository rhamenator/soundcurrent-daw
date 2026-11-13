# SPDX-License-Identifier: GPL-3.0-only
"""Explicit selected bytes do not establish runtime dependency resolution."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import restoration_library_bindings as binding
spec = importlib.util.spec_from_file_location('direct_fixtures', ROOT / 'tests/restoration_direct_dependency_tests.py')
fixtures = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixtures)


class Bindings(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.Inventory(); self.f.setUp()
        self.context = dict(self.f.f.context, reports_sha256=self.f.f.pin)
        self.inventory = self.f.inventory()
        self.plan = self.f.directory.parent / 'selections.json'
        self.library = self.f.directory.parent / 'explicit candidate α.bin'
        self.library.write_bytes(b'explicit selected candidate bytes; not a qualified native library')
        self.row = dict(lookupName='libcrypto.so.3', path=str(self.library.absolute()),
                        bytes=self.library.stat().st_size,
                        sha256=hashlib.sha256(self.library.read_bytes()).hexdigest())
        self.data = dict(format='sc-restoration-library-selections-v1',
                         **{k: self.inventory[k] for k in ['sourceCommit', 'nativePlatform', 'collectionSha256',
                                                          'candidateSha256', 'reportsSha256']}, files=[self.row])
        self.repin()

    def tearDown(self): self.f.tearDown()

    def repin(self):
        self.plan.write_text(json.dumps(self.data), encoding='utf-8')
        self.pin = hashlib.sha256(self.plan.read_bytes()).hexdigest()

    def bind(self, **context):
        with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('Binding executed a process')):
            return binding.bind_libraries(self.f.directory, self.f.candidate, self.plan,
                                          selections_sha256=self.pin, **dict(self.context, **context))

    def test_partial_selection_is_read_only_and_never_resolution_or_licensing(self):
        before = {p: p.read_bytes() for p in self.f.directory.parent.rglob('*') if p.is_file()}
        result = self.bind()
        self.assertEqual(result['selectedFiles'], [self.row]); self.assertEqual(result['unboundNames'], ['libc.so.6'])
        self.assertEqual(result['directRoles'], self.inventory['roles']); self.assertTrue(result['selectedBytesVerified'])
        for key in binding.FLAGS + ['loaderResolutionQualified', 'dependencyAbiQualified', 'dependencyLicensingQualified']:
            self.assertIs(result[key], False)
        self.assertEqual(before, {p: p.read_bytes() for p in self.f.directory.parent.rglob('*') if p.is_file()})

    def test_empty_selection_keeps_every_name_unbound(self):
        self.data['files'] = []; self.repin()
        self.assertEqual(self.bind()['unboundNames'], ['libc.so.6', 'libcrypto.so.3'])

    def test_context_schema_name_and_extent_refusals(self):
        original = json.loads(json.dumps(self.data))
        changes = [('sourceCommit', '0' * 40), ('nativePlatform', 'win32'), ('reportsSha256', '0' * 64),
                   ('format', 'unknown'), ('files', [dict(self.row, lookupName='unknown.so')]),
                   ('files', [self.row, self.row]), ('files', [dict(self.row, bytes=True)]),
                   ('files', [dict(self.row, bytes=0)]), ('files', [dict(self.row, sha256='0' * 64)]),
                   ('files', [dict(self.row, path='../relative')]), ('files', [dict(self.row, extra=True)]),
                   ('files', [dict(self.row, bytes=binding.MAX_FILE + 1)])]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                self.data = dict(original, **{key: value}); self.repin()
                with self.assertRaises(RuntimeError): self.bind()
        self.data = original; self.data['extra'] = False; self.repin()
        with self.assertRaises(RuntimeError): self.bind()

    def test_original_selection_pin_and_duplicate_json_refuse(self):
        self.plan.write_text('{}', encoding='utf-8')
        with self.assertRaises(RuntimeError): self.bind()
        self.plan.write_text('{"files":[],"files":[]}', encoding='utf-8')
        self.pin = hashlib.sha256(self.plan.read_bytes()).hexdigest()
        with self.assertRaises(RuntimeError): self.bind()

    def test_file_change_after_first_read_refuses(self):
        real = binding.selected_bytes; calls = []
        def changed(row):
            result = real(row); calls.append(1)
            if len(calls) == 1: self.library.write_bytes(b'changed')
            return result
        with mock.patch.object(binding, 'selected_bytes', side_effect=changed):
            with self.assertRaises(RuntimeError): self.bind()

    def test_changed_selection_during_inventory_refuses(self):
        real = binding.inventory_dependencies; calls = []
        def changed(*args, **kwargs):
            result = real(*args, **kwargs); calls.append(1)
            if len(calls) == 2: self.plan.write_bytes(b'{}')
            return result
        with mock.patch.object(binding, 'inventory_dependencies', side_effect=changed):
            with self.assertRaises(RuntimeError): self.bind()

    def test_size_and_total_limits_refuse_before_large_reads(self):
        with mock.patch.object(binding, 'MAX_TOTAL', self.row['bytes'] - 1):
            with self.assertRaises(RuntimeError): self.bind()
        with mock.patch.object(binding, 'MAX_FILES', 0):
            with self.assertRaises(RuntimeError): self.bind()

    def test_links_and_directories_refuse(self):
        self.data['files'][0] = dict(self.row, path=str(self.library.parent)); self.repin()
        with self.assertRaises(RuntimeError): self.bind()
        link = self.library.with_name('link.bin')
        try: link.symlink_to(self.library)
        except OSError: return  # Windows without link privilege still exercises plain-file refusals.
        self.data['files'][0] = dict(self.row, path=str(link)); self.repin()
        with self.assertRaises(RuntimeError): self.bind()

    @unittest.skipUnless(hasattr(os, 'mkfifo'), 'POSIX special-file replacement contract')
    def test_replaced_fifo_is_refused_without_blocking(self):
        real_open = os.open
        def replace(path, flags, *args, **kwargs):
            if Path(path) == self.library:
                self.library.unlink(); os.mkfifo(self.library)
            return real_open(path, flags, *args, **kwargs)
        with mock.patch.object(os, 'open', side_effect=replace):
            with self.assertRaises(RuntimeError): self.bind()

    def test_native_windows_main_selects_binding_contract(self):
        import re
        source = (ROOT / '.github/workflows/ci.yml').read_text()
        main = source.split('suite: main', 1)[1].split('suite:', 1)[0]
        pattern = re.search(r'tests: "([^"\n]+)"', main)[1]
        self.assertIsNotNone(re.fullmatch(pattern, 'restoration-library-bindings'))

    def test_cli_shared_native_path_preserves_bytes_and_flags(self):
        argv = [sys.executable, str(ROOT / 'tools/restoration_library_bindings.py'), '--reports', str(self.f.directory),
                '--candidate', str(self.f.candidate), '--selections', str(self.plan), '--selections-sha256', self.pin]
        for key, value in self.context.items(): argv += ['--' + key.replace('_', '-'), value]
        expected = self.bind(); child = subprocess.run(argv, capture_output=True, timeout=20)
        self.assertEqual(child.returncode, 0, child.stderr); self.assertEqual(json.loads(child.stdout), expected)


if __name__ == '__main__': unittest.main()
