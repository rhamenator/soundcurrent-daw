# SPDX-License-Identifier: GPL-3.0-only
"""Bounded inspector contracts; synthetic layouts do not qualify native execution."""
import importlib.util
import json
import os
import re
import subprocess
import time
import threading
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


class ReportReader(unittest.TestCase):
    def setUp(self):
        self.f = Reports(); self.f.setUp()
        fixtures.tool.collect(self.f.f.args, self.f.f.source, 'linux'); self.f.f.stage()
        self.f.capture()
        self.directory = self.f.output; self.candidate = self.f.f.top / 'candidate'
        self.data = json.loads((self.directory / 'reports.json').read_text())
        self.data['inspector']['path'] = '/usr/bin/readelf'
        for row in self.data['reports']:
            p = self.directory / row['command']; command = json.loads(p.read_text())
            command['argv'][0] = '/usr/bin/readelf'; p.write_text(json.dumps(command))
        self.repin()
        self.original = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        self.context = dict(expected_source=self.f.f.head, platform='linux',
                            collection_sha256=reports.digest(self.candidate / 'provenance/collection.json'),
                            candidate_sha256=reports.digest(self.candidate / 'candidate.json'))

    def tearDown(self):
        self.f.tearDown()

    def repin(self):
        for row in self.data['fileBindings']:
            p = self.directory / row['path']; row.update(bytes=p.stat().st_size, sha256=reports.digest(p))
        (self.directory / 'reports.json').write_text(json.dumps(self.data))
        self.pin = reports.digest(self.directory / 'reports.json')

    def verify(self, **changes):
        context = dict(self.context, reports_sha256=self.pin); context.update(changes)
        with mock.patch.object(reports.subprocess, 'Popen', side_effect=AssertionError('Reader executed a process')):
            return reports.verify_reports(self.directory, self.candidate, **context)

    def test_synthetic_windows_reports_remain_readable_without_native_execution(self):
        other = Reports(); other.setUp()
        try:
            other.f.test_stage_windows_contract_uses_flat_exe_payload(); data = other.capture('win32')
            data['inspector']['path'] = r'C:\tools\dumpbin.exe'
            for row in data['reports']:
                p = other.output / row['command']; command = json.loads(p.read_text())
                command['argv'][0] = data['inspector']['path']
                command['argv'][3] = 'C:/bundle/' + row['imagePath']; p.write_text(json.dumps(command))
            for row in data['fileBindings']:
                p = other.output / row['path']; row.update(bytes=p.stat().st_size, sha256=reports.digest(p))
            manifest = other.output / 'reports.json'; manifest.write_text(json.dumps(data))
            with mock.patch.object(reports.subprocess, 'Popen', side_effect=AssertionError('Reader executed a process')):
                result = reports.verify_reports(other.output, other.f.top / 'candidate', expected_source=other.f.head,
                        platform='win32', collection_sha256=data['collectionSha256'],
                        candidate_sha256=data['candidateSha256'], reports_sha256=reports.digest(manifest))
            self.assertTrue(result['reportsVerified']); self.assertEqual(result['nativePlatform'], 'win32')
            self.assertIs(result['nativeExecutionPerformed'], False)
        finally:
            other.tearDown()

    def test_reader_is_read_only_and_keeps_authority_false(self):
        before = {p.name: p.read_bytes() for p in self.directory.iterdir()}
        result = self.verify()
        self.assertTrue(result['reportsVerified']); self.assertEqual(result['reportsSha256'], self.pin)
        for name in ['nativeExecutionPerformed', 'dependencyClosureQualified', 'dependencySourceBundleQualified',
                     'standaloneBundleQualified', 'packageAdmissionGranted', 'installerQualified', 'installedGuiQualified']:
            self.assertIs(result[name], False)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.directory.iterdir()})
        with self.assertRaises(RuntimeError): self.verify(reports_sha256='0' * 64)
        with self.assertRaises(RuntimeError): self.verify(reports_sha256=True)
        with self.assertRaises(RuntimeError): self.verify(expected_source='0' * 40)
        with self.assertRaises(RuntimeError): self.verify(platform='win32')
        with self.assertRaises(RuntimeError): self.verify(candidate_sha256='0' * 64)
        with self.assertRaises(RuntimeError): self.verify(collection_sha256='0' * 64)

    def test_semantic_mutations_are_refused_even_with_new_manifest_pin(self):
        mutations = [
            lambda d: d.update(packageAdmissionGranted=True),
            lambda d: d.update(reportsCaptured=1),
            lambda d: d.update(extra='unknown'),
            lambda d: d.update(sourceCommit='0' * 40),
            lambda d: d.update(candidateSha256='0' * 64),
            lambda d: d['inspector'].update(bytes=True),
            lambda d: d['inspector'].update(path='/usr/bin/other'),
            lambda d: d['reports'][0].update(imageBytes=True),
            lambda d: d['reports'][0].update(imageSha256='0' * 64),
            lambda d: d['reports'][0].update(exitCode=False),
            lambda d: d['reports'][0].update(stdout='../escape'),
            lambda d: d['reports'].append(d['reports'][0]),
            lambda d: d['fileBindings'].append(d['fileBindings'][0]),
            lambda d: d['fileBindings'][0].update(path='../escape'),
            lambda d: d['fileBindings'][0].update(bytes=True),
        ]
        original = json.loads(self.original['reports.json'])
        for index, change in enumerate(mutations):
            with self.subTest(mutation=index):
                self.data = json.loads(json.dumps(original)); change(self.data)
                path = self.directory / 'reports.json'; path.write_text(json.dumps(self.data)); self.pin = reports.digest(path)
                with self.assertRaises(RuntimeError): self.verify()

    def test_command_and_result_semantics_are_checked_beyond_hashes(self):
        report = self.data['reports'][0]
        cases = [('command', lambda d: d['argv'].__setitem__(1, '--execute')),
                 ('command', lambda d: d['argv'].__setitem__(3, '/different/helper')),
                 ('command', lambda d: d['argv'].__setitem__(3, 'relative/' + report['imagePath'])),
                 ('result', lambda d: d.update(seconds=float('nan'))),
                 ('result', lambda d: d.update(exitCode=False)),
                 ('result', lambda d: d.update(stdoutBytes=True)),
                 ('result', lambda d: d.update(errors=['failure']))]
        for key, change in cases:
            with self.subTest(key=key, change=str(change)):
                for name, content in self.original.items(): (self.directory / name).write_bytes(content)
                self.data = json.loads(self.original['reports.json'])
                path = self.directory / report[key]; data = json.loads(path.read_text()); change(data)
                path.write_text(json.dumps(data)); self.repin()
                with self.assertRaises(RuntimeError): self.verify()

    def test_changed_linked_pending_extra_and_duplicate_json_are_refused(self):
        name = self.data['reports'][0]['stdout']; path = self.directory / name
        path.write_bytes(b'changed')
        with self.assertRaises(RuntimeError): self.verify()
        path.write_bytes(self.original[name])
        for name in ['extra', 'reports.pending.json']:
            p = self.directory / name; p.write_bytes(b'pending')
            with self.assertRaises(RuntimeError): self.verify()
            p.unlink()
        p = self.directory / 'extra'; p.mkdir()
        with self.assertRaises(RuntimeError): self.verify()
        p.rmdir()
        try:
            path.unlink(); path.symlink_to(self.candidate / 'candidate.json')
        except OSError:
            pass  # Native Windows may deny unprivileged symlinks; other refusals still run.
        else:
            with self.assertRaises(RuntimeError): self.verify()
        finally:
            if path.is_symlink(): path.unlink()
            path.write_bytes(self.original[path.name])
        manifest = self.directory / 'reports.json'; raw = manifest.read_bytes()
        manifest.write_bytes(raw[:-1] + b',"reportsCaptured":true}')
        self.pin = reports.digest(manifest)
        with self.assertRaises(RuntimeError): self.verify()

    def test_final_report_and_candidate_readback_refuse_mutation(self):
        real_read = reports.read; target = self.directory / self.data['reports'][0]['stdout']; seen = 0
        def changed(path, maximum=2 * 1024 * 1024):
            nonlocal seen
            raw = real_read(path, maximum)
            if path == target:
                seen += 1
                if seen == 1: target.write_bytes(b'changed')
            return raw
        with mock.patch.object(reports, 'read', side_effect=changed):
            with self.assertRaises(RuntimeError): self.verify()
        target.write_bytes(self.original[target.name]); real_verify = reports.verify_staged_payload; calls = 0
        def changed_candidate(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 2: raise RuntimeError('Injected candidate mutation')
            return real_verify(*args, **kwargs)
        with mock.patch.object(reports, 'verify_staged_payload', side_effect=changed_candidate):
            with self.assertRaises(RuntimeError): self.verify()

    def test_cli_reads_and_rejects_conflicting_modes_without_output(self):
        cmd = [sys.executable, str(ROOT / 'tools/restoration_dependency_reports.py'), '--candidate', str(self.candidate),
               '--expected-source', self.context['expected_source'], '--collection-sha256', self.context['collection_sha256'],
               '--candidate-sha256', self.context['candidate_sha256']]
        read_args = ['--verify-reports', str(self.directory), '--platform', 'linux', '--reports-sha256', self.pin]
        p = subprocess.run(cmd + read_args, capture_output=True)
        self.assertEqual(p.returncode, 0, p.stderr); self.assertTrue(json.loads(p.stdout)['reportsVerified'])
        output = self.f.f.top / 'never-created'
        for extra in [['--output', str(output)], ['--inspector', '/missing']]:
            self.assertNotEqual(subprocess.run(cmd + read_args + extra, capture_output=True).returncode, 0)
        for extra in [[], ['--verify-reports', str(self.directory)], ['--output', str(output), '--platform', 'linux'],
                      ['--output', str(output), '--reports-sha256', self.pin]]:
            self.assertNotEqual(subprocess.run(cmd + extra, capture_output=True).returncode, 0)
        self.assertFalse(output.exists())


class BoundedProcess(unittest.TestCase):
    def test_callers_cannot_raise_or_disable_resource_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for kwargs in [dict(maximum=0), dict(maximum=reports.MAX_LOG + 1), dict(maximum=True),
                           dict(timeout=0), dict(timeout=31), dict(timeout=True)]:
                with self.assertRaises(RuntimeError):
                    reports.run_report([sys.executable, '-c', "print('unexpected')"], root / 'out', root / 'err', **kwargs)
                self.assertFalse((root / 'out').exists())

    def test_blocked_pipe_close_and_process_wait_cannot_prevent_failure_record(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            released = threading.Event()
            readers = []
            class Pipe:
                def read(self, size):
                    readers.append(threading.current_thread())
                    released.wait(3)
                    return b''
                def close(self):
                    if threading.current_thread() is threading.main_thread():
                        raise AssertionError('Parent must not close a blocked pipe')
            process = mock.Mock(stdout=Pipe(), stderr=Pipe())
            process.wait.side_effect = subprocess.TimeoutExpired('inspector', 5)
            start = time.monotonic()
            try:
                with mock.patch.object(reports.subprocess, 'Popen', return_value=process), \
                     mock.patch.object(reports.os, 'killpg', create=True):
                    with self.assertRaises(RuntimeError):
                        reports.run_report(['inspector'], root / 'blocked.stdout',
                                           root / 'blocked.stderr', timeout=.05)
                self.assertLess(time.monotonic() - start, 2)
                result = json.loads((root / 'blocked.result.json').read_text())
                self.assertIn('Inspector did not terminate', result['errors'])
                self.assertIn('Inspector pipe did not close', result['errors'])
            finally:
                released.set()
                for reader in readers:
                    reader.join(timeout=1)

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
