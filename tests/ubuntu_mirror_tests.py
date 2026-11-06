#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Isolated source files only; never modify the host APT configuration."""
from pathlib import Path
import importlib.util, json, os, tempfile, unittest
from unittest import mock

repo = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mirror', repo / '.github/scripts/prepare-ubuntu-mirror.py')
mirror = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mirror)


class MirrorTests(unittest.TestCase):
    def test_deb822_preserves_trust_and_unrelated_fields(self):
        original = ('# http://azure.archive.ubuntu.com/ubuntu\r\nTypes: deb deb-src\r\n'
                    'URIs: http://azure.archive.ubuntu.com/ubuntu/ https://custom.invalid/ubuntu\r\n'
                    ' https://azure.archive.ubuntu.com/ubuntu # URI comment\r\n'
                    'Suites: noble noble-updates\r\nComponents: main universe restricted multiverse\r\n'
                    'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\r\n'
                    'Enabled: yes\r\n\r\nTypes: deb\r\nURIs: http://security.ubuntu.com/ubuntu\r\n'
                    'Suites: noble-security\r\nSigned-By: /keys/other.gpg\r\n')
        expected = original.replace('URIs: http://azure.archive.ubuntu.com/ubuntu/', 'URIs: ' + mirror.PRIMARY + '/')
        expected = expected.replace(' https://azure.archive.ubuntu.com/ubuntu #', ' ' + mirror.PRIMARY + ' #')
        self.assertEqual(mirror.rewrite(original, True), expected)
        self.assertEqual(mirror.rewrite(expected, True), expected)

    def test_legacy_sources_and_exact_uri_boundary(self):
        for scheme in ('http', 'https'):
            for slash in ('', '/'):
                with self.subTest(scheme=scheme, slash=slash):
                    uri = scheme + '://azure.archive.ubuntu.com/ubuntu' + slash
                    original = 'deb-src [arch=amd64 signed-by=/keys/ubuntu.gpg] ' + uri + ' noble main # ' + uri + '\n'
                    self.assertEqual(mirror.rewrite(original, False), original.replace(uri, mirror.PRIMARY + slash, 1))
        for uri in ('http://azure.archive.ubuntu.com.evil/ubuntu', 'http://azure.archive.ubuntu.com/ubuntu-other',
                    'http://user:password@azure.archive.ubuntu.com/ubuntu', 'https://archive.ubuntu.com/ubuntu',
                    'http://security.ubuntu.com/ubuntu'):
            text = 'deb ' + uri + ' noble main\n'
            self.assertEqual(mirror.rewrite(text, False), text)

    def test_reviewed_runner_mirror_list_uri(self):
        for deb822, original in [(True, 'Types: deb\nURIs: mirror+file:/etc/apt/apt-mirrors.txt\nSuites: noble noble-updates noble-backports noble-security\nComponents: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n'),
                                 (False, 'deb [arch=amd64 signed-by=/keys/ubuntu.gpg] mirror+file:/etc/apt/apt-mirrors.txt noble main\n')]:
            expected = original.replace(mirror.MIRROR_LIST, mirror.PRIMARY)
            counts = dict(primaryUriTokens=0, replacedMirrorListUris=0)
            self.assertEqual(mirror.rewrite(original, deb822, counts), expected)
            self.assertEqual(counts, dict(primaryUriTokens=1, replacedMirrorListUris=1))
            self.assertEqual(mirror.rewrite(expected, deb822), expected)
        for uri in ('mirror+file:/etc/apt/foreign.txt', 'mirror+file:/etc/apt/apt-mirrors.txt.other',
                    'mirror+http://azure.archive.ubuntu.com/ubuntu'):
            text = 'URIs: ' + uri + '\n'
            self.assertEqual(mirror.rewrite(text, True), text)

    def test_indirect_runner_file_and_no_silent_layout_noop(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory = root / 'sources.list.d'; directory.mkdir()
            path = directory / 'ubuntu.sources'
            original = b'URIs: mirror+file:/etc/apt/apt-mirrors.txt\nSuites: noble noble-security\nSigned-By: /keys/ubuntu.gpg\n'
            path.write_bytes(original)
            # The mirror-list file itself is never opened, followed or modified.
            mirror_list = root / 'apt-mirrors.txt'; mirror_list.write_bytes(b'unrelated untouched bytes')
            result = mirror.prepare(root)
            self.assertEqual(result['changedFiles'], 1)
            self.assertEqual(result['replacedMirrorListUris'], 1)
            self.assertEqual(result['primaryUriTokens'], 1)
            self.assertEqual(mirror_list.read_bytes(), b'unrelated untouched bytes')
            self.assertEqual(path.read_bytes(), original.replace(mirror.MIRROR_LIST.encode(), mirror.PRIMARY.encode()))
            again = mirror.prepare(root)
            self.assertEqual(again['changedFiles'], 0)
            self.assertEqual(again['primaryUriTokens'], 1)
            self.assertEqual(again['replacedMirrorListUris'], 0)
            path.write_bytes(b'# URIs: https://archive.ubuntu.com/ubuntu\nURIs: mirror+file:/etc/apt/foreign.txt\n')
            before = path.read_bytes()
            with self.assertRaisesRegex(ValueError, 'No approved primary'): mirror.prepare(root)
            self.assertEqual(path.read_bytes(), before)

    def test_non_uri_fields_and_paragraph_boundaries(self):
        for text in ('# URIs: http://azure.archive.ubuntu.com/ubuntu\n',
                     'Signed-By: http://azure.archive.ubuntu.com/ubuntu\n',
                     'URIs: https://custom.invalid\n\n http://azure.archive.ubuntu.com/ubuntu\n',
                     'URIs: https://custom.invalid\nEnabled: yes\n http://azure.archive.ubuntu.com/ubuntu\n'):
            self.assertEqual(mirror.rewrite(text, True), text)

    def test_isolated_file_mutation_modes_and_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); directory = root / 'sources.list.d'; directory.mkdir()
            path = directory / 'ubuntu.sources'; original = b'URIs: http://azure.archive.ubuntu.com/ubuntu\nSigned-By: /keys/ubuntu.gpg\n'
            path.write_bytes(original); path.chmod(0o640)
            ignored = directory / 'backup.sources.save'; ignored.write_bytes(original)
            receipt = mirror.prepare(root)
            self.assertEqual(receipt['changedFiles'], 1)
            self.assertEqual(receipt['packageAuthentication'], 'unchanged')
            self.assertNotEqual(receipt['files'][0]['beforeSha256'], receipt['files'][0]['afterSha256'])
            self.assertEqual(path.read_bytes(), original.replace(b'http://azure.archive.ubuntu.com/ubuntu', mirror.PRIMARY.encode()))
            self.assertEqual(ignored.read_bytes(), original)
            if os.name != 'nt': self.assertEqual(path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(mirror.prepare(root)['changedFiles'], 0)

    @unittest.skipIf(os.name == 'nt', 'POSIX source-link qualification for Ubuntu-only helper')
    def test_links_refused_before_any_changes(self):
        for kind in ('symlink', 'hardlink', 'directory'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); directory = root / 'sources.list.d'; directory.mkdir()
                first = directory / 'a.sources'; original = b'URIs: http://azure.archive.ubuntu.com/ubuntu\n'; first.write_bytes(original)
                bad = directory / 'z.sources'
                if kind == 'symlink': bad.symlink_to(first)
                elif kind == 'hardlink': os.link(first, bad)
                else: bad.mkdir()
                with self.assertRaises(ValueError): mirror.prepare(root)
                self.assertEqual(first.read_bytes(), original)

    def test_cli_host_guard_precedes_any_configuration_access(self):
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch('sys.argv', ['mirror', '--evidence', 'unused.json']), \
             mock.patch.object(mirror, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'GitHub-hosted'): mirror.main()
            prepare.assert_not_called()

    def test_hosted_cli_release_guard_and_audit_receipt(self):
        environment = {'GITHUB_ACTIONS': 'true', 'RUNNER_OS': 'Linux', 'RUNNER_ENVIRONMENT': 'github-hosted'}
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'receipt.json'
            with mock.patch.dict(os.environ, environment, clear=True), \
                 mock.patch('sys.argv', ['mirror', '--evidence', str(output)]), \
                 mock.patch.object(Path, 'read_text', return_value='ID=ubuntu\nVERSION_ID="24.04"\n'), \
                 mock.patch.object(mirror, 'prepare', return_value={'changedFiles': 1}) as prepare, \
                 mock.patch('builtins.print'):
                mirror.main()
                prepare.assert_called_once_with(Path('/etc/apt'))
            self.assertEqual(json.loads(output.read_text()), {'changedFiles': 1})
            with mock.patch.dict(os.environ, environment, clear=True), \
                 mock.patch('sys.argv', ['mirror', '--evidence', str(output)]), \
                 mock.patch.object(Path, 'read_text', return_value='ID=debian\nVERSION_ID="12"\n'), \
                 mock.patch.object(mirror, 'prepare') as prepare:
                with self.assertRaisesRegex(ValueError, 'Ubuntu 24.04'): mirror.main()
                prepare.assert_not_called()

    def test_workflow_uses_helper_and_preserves_test_gates(self):
        text = (repo / '.github/workflows/ci.yml').read_text()
        linux = text.split('  linux-tests:', 1)[1].split('  linux-retained-evidence:', 1)[0]
        self.assertIn('tests/ubuntu_mirror_tests.py', linux)
        self.assertIn('prepare-ubuntu-mirror.py', linux)
        self.assertIn('ci-ubuntu-mirror.json', linux)
        self.assertIn('timeout-minutes: 45', linux)
        self.assertIn('python3 .github/scripts/run-linux-test-shard.py build ${{ matrix.shard }} --count 5', linux)
        self.assertIn('ninja-build libssl-dev libsndfile1-dev qt6-base-dev qt6-l10n-tools', linux)
        self.assertNotIn('AllowUnauthenticated', linux)
        self.assertNotIn('--allow-unauthenticated', linux)
        self.assertIn('Acquire::https::Timeout=30', linux)


if __name__ == '__main__':
    unittest.main()
