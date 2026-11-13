# SPDX-License-Identifier: GPL-3.0-only
"""Direct names are an unresolved inventory, not dependency closure evidence."""
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import restoration_direct_dependencies as direct
import restoration_dependency_reports as reports
spec = importlib.util.spec_from_file_location('report_fixtures', ROOT / 'tests/restoration_dependency_tests.py')
fixtures = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixtures)


def linux(names):
    lines = [f'Dynamic section at offset 0x100 contains {len(names) + 1} entries:',
             '  Tag Type Name/Value']
    lines += [' 0x0000000000000001 (NEEDED) Shared library: [' + name + ']' for name in names]
    lines += [' 0x0000000000000000 (NULL) 0x0']
    return ('\n'.join(lines) + '\n').encode('ascii')


def windows(names):
    return ('Dump of file C:\\bundle\\helper.exe\r\n\r\nFile Type: EXECUTABLE IMAGE\r\n\r\n'
            '  Image has the following dependencies:\r\n\r\n' +
            ''.join('    ' + name + '\r\n' for name in names) + '\r\n  Summary\r\n\r\n  1000 .text\r\n').encode('ascii')


class Names(unittest.TestCase):
    def test_platform_names_order_repeats_and_lookup_case(self):
        result = direct.dependency_names(linux(['libZ.so.1', 'libc.so.6', 'libZ.so.1']), 'linux')
        self.assertEqual([r['name'] for r in result], ['libZ.so.1', 'libc.so.6', 'libZ.so.1'])
        self.assertEqual(result[0]['lookupName'], 'libZ.so.1')
        result = direct.dependency_names(windows(['KERNEL32.dll', 'MSVCP140.DLL', 'KERNEL32.dll']), 'win32')
        self.assertEqual([r['lookupName'] for r in result], ['kernel32.dll', 'msvcp140.dll', 'kernel32.dll'])
        self.assertTrue(all(r['kind'] == 'import' for r in result))

    def test_empty_linux_table_has_no_direct_names_and_no_closure_claim(self):
        self.assertEqual(direct.dependency_names(linux([]), 'linux'), [])

    def test_native_windows_main_selects_direct_inventory_contract(self):
        source = (ROOT / '.github/workflows/ci.yml').read_text()
        main = source.split('suite: main', 1)[1].split('suite:', 1)[0]
        pattern = re.search(r'tests: "([^"\n]+)"', main)[1]
        self.assertIsNotNone(re.fullmatch(pattern, 'restoration-direct-dependencies'))

    def test_malformed_unsafe_and_incomplete_reports_refuse(self):
        cases = [(b'', 'linux'), (b'unknown report', 'linux'), (linux(['libc.so.6'])[:-20], 'linux'),
                 (linux(['../libc.so.6']), 'linux'), (linux(['a\\b.so']), 'linux'),
                 (linux(['lib with spaces.so']), 'linux'), (linux(['x' * 256]), 'linux'),
                 (linux(['libc.so.6']).replace(b'2 entries', b'3 entries'), 'linux'),
                 (windows(['KERNEL32.dll']).replace(b'  Summary', b'  Missing'), 'win32'),
                 (windows(['../kernel32.dll']), 'win32'), (windows(['helper.exe']), 'win32'),
                 (windows(['kernel32.dll']).replace(b'    kernel32.dll', b'kernel32.dll'), 'win32'),
                 (windows(['kernel32.dll']).replace(b'  Summary', b'  Image has the following delay load dependencies:'), 'win32'),
                 (windows(['kernel32.dll']) + b'    other.dll\n', 'win32'),
                 (linux(['libc.so.6']) + b'\x00', 'linux')]
        for raw, platform in cases:
            with self.subTest(raw=raw[:50], platform=platform):
                with self.assertRaises(RuntimeError): direct.dependency_names(raw, platform)

    def test_resource_limits_and_platform(self):
        with self.assertRaises(RuntimeError): direct.dependency_names(b'x' * (reports.MAX_LOG + 1), 'linux')
        with self.assertRaises(RuntimeError): direct.dependency_names(linux(['libc.so'] * 257), 'linux')
        with self.assertRaises(RuntimeError): direct.dependency_names(windows(['kernel32.dll'] * 257), 'win32')
        with self.assertRaises(RuntimeError): direct.dependency_names(linux(['libc.so']), 'other')


class Inventory(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.ReportReader(); self.f.setUp()
        self.directory = self.f.directory; self.candidate = self.f.candidate
        for row in self.f.data['reports']:
            raw = linux(['libcrypto.so.3', 'libc.so.6'])
            (self.directory / row['stdout']).write_bytes(raw)
            path = self.directory / row['result']; result = json.loads(path.read_text())
            result['stdoutBytes'] = len(raw); path.write_text(json.dumps(result))
        self.f.repin()

    def tearDown(self): self.f.tearDown()

    def inventory(self, **changes):
        context = dict(self.f.context, reports_sha256=self.f.pin); context.update(changes)
        with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('Inventory executed a process')):
            return direct.inventory_dependencies(self.directory, self.candidate, **context)

    def test_four_role_inventory_is_read_only_unresolved_and_pinned(self):
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        result = self.inventory()
        self.assertTrue(result['directNamesExtracted']); self.assertEqual(result['reportsSha256'], self.f.pin)
        self.assertEqual(len(result['roles']), 4)
        self.assertTrue(all([d['name'] for d in r['dependencies']] == ['libcrypto.so.3', 'libc.so.6'] for r in result['roles']))
        self.assertTrue(all(result[flag] is False for flag in direct.FLAGS))
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})
        with self.assertRaises(RuntimeError): self.inventory(reports_sha256='0' * 64)

    def test_re_pinned_unsupported_report_does_not_publish_inventory(self):
        row = self.f.data['reports'][0]; raw = b'unknown inspector layout\n'
        (self.directory / row['stdout']).write_bytes(raw)
        path = self.directory / row['result']; data = json.loads(path.read_text()); data['stdoutBytes'] = len(raw)
        path.write_text(json.dumps(data)); self.f.repin()
        with self.assertRaises(RuntimeError): self.inventory()

    def test_actual_stdout_byte_change_between_reader_and_parser_refuses(self):
        real_verify = direct.verify_reports
        def changed(*args, **kwargs):
            result = real_verify(*args, **kwargs)
            (self.directory / self.f.data['reports'][0]['stdout']).write_bytes(linux(['libevil.so']))
            return result
        with mock.patch.object(direct, 'verify_reports', side_effect=changed):
            with self.assertRaises(RuntimeError): self.inventory()

    def test_synthetic_windows_inventory_api_and_cli_keep_native_names(self):
        other = fixtures.Reports(); other.setUp()
        try:
            other.f.test_stage_windows_contract_uses_flat_exe_payload(); data = other.capture('win32')
            data['inspector']['path'] = r'C:\VS\dumpbin.exe'
            for row in data['reports']:
                raw = windows(['KERNEL32.dll', 'sndfile.dll', 'KERNEL32.dll'])
                (other.output / row['stdout']).write_bytes(raw)
                path = other.output / row['result']; result = json.loads(path.read_text())
                result['stdoutBytes'] = len(raw); path.write_text(json.dumps(result))
                path = other.output / row['command']; command = json.loads(path.read_text())
                command['argv'][0] = data['inspector']['path']
                command['argv'][3] = 'C:/bundle/' + row['imagePath']; path.write_text(json.dumps(command))
            for row in data['fileBindings']:
                path = other.output / row['path']; row.update(bytes=path.stat().st_size, sha256=reports.digest(path))
            manifest = other.output / 'reports.json'; manifest.write_text(json.dumps(data))
            pin = reports.digest(manifest); candidate = other.f.top / 'candidate'
            context = dict(expected_source=other.f.head, platform='win32', collection_sha256=data['collectionSha256'],
                           candidate_sha256=data['candidateSha256'], reports_sha256=pin)
            before = {p.name: p.read_bytes() for p in other.output.iterdir()}
            with mock.patch.object(subprocess, 'Popen', side_effect=AssertionError('Inventory executed a process')):
                result = direct.inventory_dependencies(other.output, candidate, **context)
            self.assertTrue(all([d['name'] for d in r['dependencies']] == ['KERNEL32.dll', 'sndfile.dll', 'KERNEL32.dll']
                                for r in result['roles']))
            self.assertTrue(all([d['lookupName'] for d in r['dependencies']] == ['kernel32.dll', 'sndfile.dll', 'kernel32.dll']
                                for r in result['roles']))
            cmd = [sys.executable, str(ROOT / 'tools/restoration_direct_dependencies.py'), '--reports', str(other.output),
                   '--candidate', str(candidate), '--platform', 'win32', '--expected-source', other.f.head,
                   '--collection-sha256', data['collectionSha256'], '--candidate-sha256', data['candidateSha256'],
                   '--reports-sha256', pin]
            child = subprocess.run(cmd, capture_output=True, timeout=10)
            self.assertEqual(child.returncode, 0, child.stderr)
            self.assertEqual(json.loads(child.stdout), result)
            self.assertTrue(all(result[name] is False for name in direct.FLAGS))
            self.assertEqual(before, {p.name: p.read_bytes() for p in other.output.iterdir()})
        finally:
            other.tearDown()

    def test_cli_inventory_has_no_output_files(self):
        cmd = [sys.executable, str(ROOT / 'tools/restoration_direct_dependencies.py'),
               '--reports', str(self.directory), '--candidate', str(self.candidate), '--platform', 'linux',
               '--expected-source', self.f.context['expected_source'], '--collection-sha256', self.f.context['collection_sha256'],
               '--candidate-sha256', self.f.context['candidate_sha256'], '--reports-sha256', self.f.pin]
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        p = subprocess.run(cmd, capture_output=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr); self.assertTrue(json.loads(p.stdout)['directNamesExtracted'])
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})

if __name__ == '__main__': unittest.main()
