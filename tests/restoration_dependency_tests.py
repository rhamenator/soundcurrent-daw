# SPDX-License-Identifier: GPL-3.0-only
"""Bounded inspector contracts; synthetic layouts do not qualify native execution."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import restoration_dependency_reports as reports

spec = importlib.util.spec_from_file_location('image_fixtures', ROOT / 'tests/restoration_worker_image_tests.py')
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)


class Reports(unittest.TestCase):
    def setUp(self):
        self.f = fixtures.Images()
        self.f.setUp()
        self.output = self.f.top / 'reports'

    def tearDown(self):
        self.f.tearDown()

    def fake_report(self, argv, stdout, stderr):
        stdout.write_bytes(b'Synthetic dependency report\n')
        stderr.write_bytes(b'')
        result = dict(exitCode=0, seconds=0, stdoutBytes=stdout.stat().st_size, stderrBytes=0, errors=[])
        stdout.with_suffix('.result.json').write_text(json.dumps(result))
        return result

    def capture(self, platform='linux', **changes):
        candidate = self.f.top / 'candidate'
        context = dict(expected_source=self.f.head, platform=platform,
                       collection_sha256=reports.digest(candidate / 'provenance/collection.json'),
                       candidate_sha256=reports.digest(candidate / 'candidate.json'))
        context.update(changes)
        with mock.patch.object(reports.sys, 'platform', platform), \
             mock.patch.object(reports, 'resolve_inspector', return_value=Path(sys.executable)), \
             mock.patch.object(reports, 'run_report', side_effect=self.fake_report):
            return reports.capture_dependencies(candidate, self.output, **context)

    def test_linux_reports_bind_four_helpers_and_sixteen_files(self):
        fixtures.tool.collect(self.f.args, self.f.source, 'linux')
        self.f.stage()
        data = self.capture()
        self.assertEqual(len(list(self.output.iterdir())), 17)
        self.assertEqual(len(data['reports']), 4)
        self.assertEqual(len(data['fileBindings']), 16)
        self.assertFalse((self.output / 'reports.pending.json').exists())
        for row in data['fileBindings']:
            path = self.output / row['path']
            self.assertEqual(path.stat().st_size, row['bytes'])
            self.assertEqual(reports.digest(path), row['sha256'])
        for row in data['reports']:
            command = json.loads((self.output / row['command']).read_text())
            self.assertEqual(command['argv'][1:3], ['--dynamic', '--wide'])
            self.assertEqual(command['imageSha256'], row['imageSha256'])
        for flag in ['nativeExecutionPerformed', 'dependencyClosureQualified', 'dependencySourceBundleQualified',
                     'standaloneBundleQualified', 'packageAdmissionGranted', 'installerQualified', 'installedGuiQualified']:
            self.assertIs(data[flag], False)

    def test_synthetic_windows_uses_native_dumpbin_arguments(self):
        self.f.test_stage_windows_contract_uses_flat_exe_payload()
        data = self.capture('win32')
        self.assertEqual(data['nativePlatform'], 'win32')
        for row in data['reports']:
            command = json.loads((self.output / row['command']).read_text())
            self.assertEqual(command['argv'][1:3], ['/nologo', '/dependents'])
            self.assertTrue(command['argv'][-1].endswith('.exe'))

    def test_wrong_pin_and_existing_destination_are_refused(self):
        fixtures.tool.collect(self.f.args, self.f.source, 'linux')
        self.f.stage()
        with self.assertRaises(RuntimeError): self.capture(candidate_sha256='0' * 64)
        self.assertFalse(self.output.exists())
        self.output.mkdir()
        marker = self.output / 'preserve'; marker.write_bytes(b'prior')
        with self.assertRaises(RuntimeError): self.capture()
        self.assertEqual(marker.read_bytes(), b'prior')

    def test_inspector_failure_leaves_pending_without_final_receipt(self):
        fixtures.tool.collect(self.f.args, self.f.source, 'linux'); self.f.stage()
        self.fake_report = mock.Mock(side_effect=RuntimeError('injected inspector failure'))
        with self.assertRaises(RuntimeError): self.capture()
        self.assertTrue((self.output / 'reports.pending.json').is_file())
        self.assertFalse((self.output / 'reports.json').exists())

    def test_final_readback_refuses_image_changed_during_inspection(self):
        fixtures.tool.collect(self.f.args, self.f.source, 'linux'); staged = self.f.stage()
        original = self.fake_report
        def changed(argv, stdout, stderr):
            result = original(argv, stdout, stderr)
            image = self.f.top / 'candidate' / staged['payload'][0]['stagedPath']
            image.write_bytes(b'x' * image.stat().st_size)
            return result
        self.fake_report = changed
        with self.assertRaises(RuntimeError): self.capture()
        self.assertTrue((self.output / 'reports.pending.json').is_file())
        self.assertFalse((self.output / 'reports.json').exists())

    def test_collector_invokes_capture_only_after_staging(self):
        self.f.args.stage_output = self.f.top / 'candidate'
        self.f.args.dependency_output = self.output
        self.f.args.dependency_tool = Path(sys.executable)
        with mock.patch.object(reports, 'capture_dependencies') as capture:
            fixtures.tool.collect(self.f.args, self.f.source, 'linux')
        capture.assert_called_once()
        self.assertTrue((self.f.args.stage_output / 'candidate.json').exists())
        self.assertEqual(capture.call_args.kwargs['expected_source'], self.f.head)

    def test_host_platform_mismatch_does_not_create_output(self):
        with mock.patch.object(reports.sys, 'platform', 'linux'):
            with self.assertRaises(RuntimeError):
                reports.capture_dependencies(self.f.top / 'absent', self.output, expected_source=self.f.head,
                                             platform='win32', collection_sha256='0' * 64, candidate_sha256='0' * 64)
        self.assertFalse(self.output.exists())


class BoundedProcess(unittest.TestCase):
    def test_callers_cannot_raise_or_disable_resource_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for kwargs in [dict(maximum=0), dict(maximum=reports.MAX_LOG + 1), dict(maximum=True),
                           dict(timeout=0), dict(timeout=31), dict(timeout=True)]:
                with self.assertRaises(RuntimeError):
                    reports.run_report([sys.executable, '-c', "print('unexpected')"], root / 'out', root / 'err', **kwargs)
                self.assertFalse((root / 'out').exists())

    def test_success_failure_size_and_deadline_contracts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cases = [('success', "print('dependency')", True, 5, 1024),
                     ('failed', "print('failed');raise SystemExit(2)", False, 5, 1024),
                     ('stdout-bound', "import sys;sys.stdout.write('x'*65536)", False, 5, 512),
                     ('stderr-bound', "import sys;sys.stderr.write('x'*65536)", False, 5, 512),
                     ('deadline', 'import time;time.sleep(2)', False, .05, 1024)]
            for name, program, success, timeout, maximum in cases:
                with self.subTest(name=name):
                    out, err = root / (name + '.stdout'), root / (name + '.stderr')
                    argv = [sys.executable, '-c', program]
                    if success:
                        self.assertEqual(reports.run_report(argv, out, err, timeout=timeout, maximum=maximum)['exitCode'], 0)
                    else:
                        with self.assertRaises(RuntimeError):
                            reports.run_report(argv, out, err, timeout=timeout, maximum=maximum)
                    self.assertLessEqual(out.stat().st_size, maximum)
                    self.assertLessEqual(err.stat().st_size, maximum)
                    self.assertTrue(out.with_suffix('.result.json').exists())


if __name__ == '__main__':
    unittest.main()
