# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import restoration_library_snapshot_reader as reader
spec = importlib.util.spec_from_file_location('snapshot_fixtures', ROOT/'tests/restoration_library_snapshot_tests.py')
fixtures = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixtures)


class Reader(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.Snapshot(); self.f.setUp(); self.f.capture()
        self.root = self.f.output; self.binding_fixture = self.f.f
        self.directory = self.binding_fixture.f.directory; self.candidate = self.binding_fixture.f.candidate
        self.context = dict(self.binding_fixture.context, selections_sha256=self.binding_fixture.pin)
        self.repin()

    def tearDown(self): self.f.tearDown()

    def repin(self): self.snapshot_pin = hashlib.sha256((self.root/'snapshot.json').read_bytes()).hexdigest()

    def verify(self, **changes):
        context = dict(self.context, snapshot_sha256=self.snapshot_pin); context.update(changes)
        with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('Reader executed a process')):
            return reader.verify_snapshot(self.root, self.directory, self.candidate, **context)

    def test_read_only_verification_after_original_files_removed(self):
        self.binding_fixture.library.unlink(); self.binding_fixture.plan.unlink()
        before = {p: p.read_bytes() for p in self.root.iterdir()}
        result = self.verify(); self.assertTrue(result['snapshotVerified'])
        self.assertIs(result['originalSourcePathsAccessed'], False)
        self.assertEqual(result['selectedFiles'][0]['path'], 'file-000.bin')
        for flag in reader.DENIED: self.assertIs(result[flag], False)
        self.assertEqual(before, {p: p.read_bytes() for p in self.root.iterdir()})

    def test_explicit_snapshot_selection_and_context_pins_refuse(self):
        for key, value in [('snapshot_sha256', '0'*64), ('selections_sha256', '0'*64),
                           ('reports_sha256', '0'*64), ('expected_source', '0'*40), ('snapshot_sha256', True)]:
            with self.subTest(key=key):
                with self.assertRaises(RuntimeError): self.verify(**{key: value})

    def test_pending_extra_missing_and_changed_copy_refuse(self):
        (self.root/'snapshot.pending.json').write_bytes(b'{}')
        with self.assertRaises(RuntimeError): self.verify()
        (self.root/'snapshot.pending.json').unlink()
        path = self.root/'file-000.bin'; path.unlink()
        with self.assertRaises(RuntimeError): self.verify()
        path.write_bytes(b'corrupt')
        with self.assertRaises(RuntimeError): self.verify()

    def test_re_pinned_authority_or_changed_metadata_is_not_accepted(self):
        original = json.loads((self.root/'snapshot.json').read_bytes())
        for key, value in [('packageAdmissionGranted', True), ('nativeExecutionPerformed', 0),
                           ('unboundNames', []), ('snapshotBytesVerified', False)]:
            with self.subTest(key=key):
                data = dict(original, **{key: value}); (self.root/'snapshot.json').write_text(json.dumps(data))
                self.repin()
                with self.assertRaises(RuntimeError): self.verify()

    def test_original_posix_and_windows_paths_are_opaque_provenance(self):
        for path in ['/unavailable/δ/lib.so', r'Q:\unavailable\δ\library.dll']:
            plan = json.loads((self.root/'selections.json').read_bytes()); plan['files'][0]['path'] = path
            raw = json.dumps(plan).encode(); (self.root/'selections.json').write_bytes(raw)
            self.context['selections_sha256'] = hashlib.sha256(raw).hexdigest()
            binding = json.loads((self.root/'bindings.json').read_bytes())
            binding['selectedFiles'] = plan['files']; binding['selectionsSha256'] = self.context['selections_sha256']
            raw = json.dumps(binding).encode(); (self.root/'bindings.json').write_bytes(raw)
            receipt = json.loads((self.root/'snapshot.json').read_bytes())
            receipt['selectionsSha256'] = self.context['selections_sha256']; receipt['bindingsSha256'] = hashlib.sha256(raw).hexdigest()
            (self.root/'snapshot.json').write_text(json.dumps(receipt)); self.repin()
            self.assertTrue(self.verify()['snapshotVerified'])

    def test_changed_copy_after_first_read_refuses(self):
        real = reader.selected_bytes; calls = []
        def changed(row):
            result = real(row); calls.append(1)
            if len(calls) == 1: (self.root/'file-000.bin').write_bytes(b'changed')
            return result
        with mock.patch.object(reader, 'selected_bytes', side_effect=changed):
            with self.assertRaises(RuntimeError): self.verify()

    def test_changed_provenance_after_inventory_read_refuses(self):
        real = reader.inventory_dependencies; calls = []
        def changed(*args, **kwargs):
            result = real(*args, **kwargs); calls.append(1)
            if len(calls) == 2: (self.root/'bindings.json').write_bytes(b'changed')
            return result
        with mock.patch.object(reader, 'inventory_dependencies', side_effect=changed):
            with self.assertRaises(RuntimeError): self.verify()

    def test_resource_bounds_and_duplicate_json_refuse(self):
        with mock.patch.object(reader, 'MAX_TOTAL', 1):
            with self.assertRaises(RuntimeError): self.verify()
        (self.root/'snapshot.json').write_bytes(b'{"format":1,"format":2}'); self.repin()
        with self.assertRaises(RuntimeError): self.verify()

    def test_empty_selection_preserves_all_unbound_names(self):
        self.binding_fixture.data['files'] = []; self.binding_fixture.repin()
        self.root = self.root.with_name('empty-snapshot')
        self.context['selections_sha256'] = self.binding_fixture.pin
        fixtures.snapshot.snapshot_libraries(self.directory, self.candidate, self.binding_fixture.plan,
                                             self.root, selections_sha256=self.binding_fixture.pin,
                                             **self.binding_fixture.context)
        self.repin(); result = self.verify()
        self.assertEqual(result['selectedFiles'], [])
        self.assertEqual(result['unboundNames'], ['libc.so.6', 'libcrypto.so.3'])

    def test_re_pinned_unknown_lookup_and_bool_extent_refuse(self):
        original = json.loads((self.root/'selections.json').read_bytes())
        for key, value in [('lookupName', 'unknown.dll'), ('bytes', True)]:
            plan = json.loads(json.dumps(original)); plan['files'][0][key] = value
            raw = json.dumps(plan).encode(); (self.root/'selections.json').write_bytes(raw)
            self.context['selections_sha256'] = hashlib.sha256(raw).hexdigest()
            with self.assertRaises(RuntimeError): self.verify()

    def test_cli_reads_snapshot_without_original_source(self):
        self.binding_fixture.library.unlink(); self.binding_fixture.plan.unlink()
        cmd = [sys.executable, str(ROOT/'tools/restoration_library_snapshot_reader.py'), '--snapshot', str(self.root),
               '--reports', str(self.directory), '--candidate', str(self.candidate), '--snapshot-sha256', self.snapshot_pin]
        for key, value in self.context.items(): cmd += ['--'+key.replace('_', '-'), value]
        child = subprocess.run(cmd, capture_output=True, timeout=20)
        self.assertEqual(child.returncode, 0, child.stderr); self.assertEqual(json.loads(child.stdout), self.verify())

    def test_linked_copy_refuses(self):
        path = self.root/'file-000.bin'; path.unlink()
        try: path.symlink_to(self.binding_fixture.library)
        except OSError: return  # Native Windows may lack symlink privilege.
        with self.assertRaises(RuntimeError): self.verify()

    def test_native_windows_main_selects_reader_contract(self):
        import re
        main = (ROOT/'.github/workflows/ci.yml').read_text().split('suite: main', 1)[1].split('suite:', 1)[0]
        pattern = re.search(r'tests: "([^"\n]+)"', main)[1]
        self.assertIsNotNone(re.fullmatch(pattern, 'restoration-library-snapshot-reader'))


if __name__ == '__main__': unittest.main()
