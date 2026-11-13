# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import restoration_library_snapshot as snapshot
spec = importlib.util.spec_from_file_location('binding_fixtures', ROOT/'tests/restoration_library_binding_tests.py')
fixtures = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixtures)


class Snapshot(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.Bindings(); self.f.setUp()
        self.output = self.f.library.parent/'snapshot'

    def tearDown(self): self.f.tearDown()

    def capture(self):
        with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('Snapshot executed a process')):
            return snapshot.snapshot_libraries(self.f.f.directory, self.f.f.candidate, self.f.plan,
                                               self.output, selections_sha256=self.f.pin, **self.f.context)

    def test_checked_copy_provenance_and_no_execution_authority(self):
        result = self.capture(); self.assertTrue(result['snapshotBytesVerified'])
        self.assertEqual((self.output/'file-000.bin').read_bytes(), self.f.library.read_bytes())
        self.assertEqual((self.output/'selections.json').read_bytes(), self.f.plan.read_bytes())
        self.assertEqual(result, json.loads((self.output/'snapshot.json').read_bytes()))
        self.assertEqual(result['unboundNames'], ['libc.so.6'])
        self.assertEqual({p.name for p in self.output.iterdir()},
                         {'snapshot.json', 'selections.json', 'bindings.json', 'file-000.bin'})
        for key, value in result.items():
            if key.endswith('Qualified') or key in ['packageAdmissionGranted', 'nativeExecutionPerformed']:
                self.assertIs(value, False)

    def test_existing_output_is_preserved(self):
        self.output.mkdir(); (self.output/'owner.txt').write_text('preserved')
        with self.assertRaises(RuntimeError): self.capture()
        self.assertEqual((self.output/'owner.txt').read_text(), 'preserved')

    def test_interrupted_copy_keeps_pending_not_final(self):
        with mock.patch.object(snapshot, 'copy_selected', side_effect=OSError('injected copy failure')):
            with self.assertRaises(OSError): self.capture()
        self.assertTrue((self.output/'snapshot.pending.json').is_file())
        self.assertFalse((self.output/'snapshot.json').exists())

    def test_source_change_after_copy_refuses_final_receipt(self):
        real = snapshot.copy_selected
        def changed(row, destination):
            real(row, destination); self.f.library.write_bytes(b'changed')
        with mock.patch.object(snapshot, 'copy_selected', side_effect=changed):
            with self.assertRaises(RuntimeError): self.capture()
        self.assertFalse((self.output/'snapshot.json').exists())

    def test_copy_corruption_refuses_publication(self):
        real = snapshot.copy_selected
        def corrupt(row, destination):
            real(row, destination); destination.write_bytes(b'corrupt')
        with mock.patch.object(snapshot, 'copy_selected', side_effect=corrupt):
            with self.assertRaises(RuntimeError): self.capture()
        self.assertFalse((self.output/'snapshot.json').exists())

    def test_extra_file_refuses_publication(self):
        real = snapshot.copy_selected
        def extra(row, destination):
            real(row, destination); (self.output/'extra.bin').write_bytes(b'extra')
        with mock.patch.object(snapshot, 'copy_selected', side_effect=extra):
            with self.assertRaises(RuntimeError): self.capture()
        self.assertFalse((self.output/'snapshot.json').exists())

    def test_checked_input_directory_cannot_be_snapshot_output(self):
        self.output = self.f.f.directory/'new-output'
        with self.assertRaises(RuntimeError): self.capture()
        self.assertFalse(self.output.exists())

    def test_changed_provenance_refuses_publication(self):
        real = snapshot.bind_libraries; calls = []
        def changed(*args, **kwargs):
            result = real(*args, **kwargs); calls.append(1)
            if len(calls) == 2: (self.output/'bindings.json').write_bytes(b'changed')
            return result
        with mock.patch.object(snapshot, 'bind_libraries', side_effect=changed):
            with self.assertRaises(RuntimeError): self.capture()
        self.assertFalse((self.output/'snapshot.json').exists())

    def test_cli_native_host_checked_copy(self):
        cmd = [sys.executable, str(ROOT/'tools/restoration_library_snapshot.py'), '--reports', str(self.f.f.directory),
               '--candidate', str(self.f.f.candidate), '--selections', str(self.f.plan), '--output', str(self.output),
               '--selections-sha256', self.f.pin]
        for key, value in self.f.context.items(): cmd += ['--'+key.replace('_', '-'), value]
        child = subprocess.run(cmd, capture_output=True, timeout=20)
        self.assertEqual(child.returncode, 0, child.stderr)
        self.assertEqual(json.loads(child.stdout), json.loads((self.output/'snapshot.json').read_bytes()))

    def test_native_windows_main_selects_snapshot_contract(self):
        import re
        main = (ROOT/'.github/workflows/ci.yml').read_text().split('suite: main', 1)[1].split('suite:', 1)[0]
        pattern = re.search(r'tests: "([^"\n]+)"', main)[1]
        self.assertIsNotNone(re.fullmatch(pattern, 'restoration-library-snapshot'))


if __name__ == '__main__': unittest.main()
