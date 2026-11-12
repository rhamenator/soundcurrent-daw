# SPDX-License-Identifier: GPL-3.0-only
"""Bounded inspector contracts; synthetic layouts do not qualify native execution."""
import importlib.util
import json
import os
import re
import subprocess
import time
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
        def observed(candidate, output, **context):
            self.assertTrue((candidate / 'candidate.json').is_file())
            self.assertFalse((candidate / 'candidate.pending.json').exists())
            checked = reports.verify_staged_payload(candidate, expected_source=context['expected_source'],
                        platform=context['platform'], collection_sha256=context['collection_sha256'],
                        candidate_sha256=context['candidate_sha256'])
            self.assertTrue(checked['provenanceVerified'])
        with mock.patch.object(reports, 'capture_dependencies', side_effect=observed) as capture:
            fixtures.tool.collect(self.f.args, self.f.source, 'linux')
        capture.assert_called_once()
        self.assertTrue((self.f.args.stage_output / 'candidate.json').exists())
        self.assertEqual(capture.call_args.kwargs['expected_source'], self.f.head)

    def test_library_request_without_staging_is_rejected_before_source_work(self):
        self.f.args.dependency_output = self.output
        self.f.args.stage_output = None
        with mock.patch.object(fixtures.tool, 'source_snapshot', side_effect=AssertionError('source work started')):
            with self.assertRaises(RuntimeError): fixtures.tool.collect(self.f.args, self.f.source, 'linux')
        self.assertFalse(self.output.exists())

    def test_cli_dependency_mode_conflicts_do_not_create_output(self):
        common = [sys.executable, str(ROOT / 'tools/restoration_worker_images.py'), '--expected-source', self.f.head]
        output = str(self.output); stage = str(self.f.top / 'candidate')
        cases = [(['--dependency-tool', 'unused'], 'Dependency tool requires report output'),
                 (['--dependency-output', output], 'Dependency reports require staged output'),
                 (['--verify-staged', stage, '--collection-sha256', '0'*64, '--candidate-sha256', '0'*64,
                   '--stage-output', stage, '--dependency-output', output], 'Conflicting/incomplete staged'),
                 (['--verify-profile', stage, '--collection-sha256', '0'*64, '--stage-output', stage,
                   '--dependency-output', output], 'Conflicting/incomplete profile'),
                 (['--snapshot-inputs', str(self.f.top / 'snapshot.json'), '--stage-output', stage,
                   '--dependency-output', output], 'Conflicting/existing snapshot')]
        for args, message in cases:
            result = subprocess.run(common + args, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(message, result.stderr)
            self.assertFalse(self.output.exists())

    def test_capture_output_bound_failure_keeps_pending(self):
        fixtures.tool.collect(self.f.args, self.f.source, 'linux'); self.f.stage()
        real_run = reports.run_report
        def failed(argv, stdout, stderr):
            return real_run([sys.executable, '-c', "print('x'*65536)"], stdout, stderr, maximum=512)
        self.fake_report = failed
        with self.assertRaises(RuntimeError): self.capture()
        self.assertTrue((self.output / 'reports.pending.json').is_file())
        self.assertFalse((self.output / 'reports.json').exists())
        self.assertLessEqual((self.output / 'asset.stdout').stat().st_size, 512)

    def test_inspector_resolver_checks_names_and_extent(self):
        tool = self.f.top / 'readelf'; tool.write_bytes(b'x')
        self.assertEqual(reports.resolve_inspector('linux', tool), tool.resolve())
        wrong = self.f.top / 'evilreadelf'; wrong.write_bytes(b'x')
        with self.assertRaises(RuntimeError): reports.resolve_inspector('linux', wrong)
        tool.write_bytes(b'')
        with self.assertRaises(RuntimeError): reports.resolve_inspector('linux', tool)
        with self.assertRaises(RuntimeError): reports.resolve_inspector('darwin', tool)
        win = self.f.top / 'dumpbin.exe'; win.write_bytes(b'x')
        self.assertEqual(reports.resolve_inspector('win32', win), win.resolve())

    def test_native_windows_main_selects_the_report_contract(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        main = workflow.split('suite: main', 1)[1]
        selection = re.search(r'tests: "([^"]+)"', main).group(1)
        self.assertIsNotNone(re.fullmatch(selection, 'restoration-dependency-reports'))

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

    @unittest.skipIf(os.name == 'nt', 'POSIX owned inspector process-group cleanup contract')
    def test_pipe_holding_child_cannot_extend_the_work_deadline(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            program = "import subprocess,sys;subprocess.Popen([sys.executable,'-c','import time;time.sleep(10)'])"
            started = time.monotonic()
            with self.assertRaises(RuntimeError):
                reports.run_report([sys.executable, '-c', program], root / 'child.stdout', root / 'child.stderr', timeout=.1)
            self.assertLess(time.monotonic() - started, 2)
            result = json.loads((root / 'child.result.json').read_text())
            self.assertIn('Inspector deadline exceeded', result['errors'])

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
