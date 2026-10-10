#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect retained local observations without replaying DSP or an installer."""
from pathlib import Path, PurePosixPath
import hashlib, json, re, zipfile

folder = Path(__file__).resolve().parent
manifest = json.loads((folder / 'manifest.json').read_bytes())
path = folder / 'capture.zip'
sha = lambda data: hashlib.sha256(data).hexdigest()
assert path.stat().st_size == manifest['bytes'] < 2 * 1024 * 1024
assert sha(path.read_bytes()) == manifest['sha256']
with zipfile.ZipFile(path) as z:
    infos = z.infolist()
    assert len(infos) == len({i.filename for i in infos}) == len(manifest['entries']) == 14
    assert set(z.namelist()) == set(manifest['entries'])
    assert sum(i.file_size for i in infos) < 4 * 1024 * 1024
    entries = {}
    for i in infos:
        p = PurePosixPath(i.filename)
        assert not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename
        data = z.read(i)
        assert len(data) == manifest['entries'][i.filename]['bytes']
        assert sha(data) == manifest['entries'][i.filename]['sha256']
        entries[i.filename] = data

summary = json.loads(entries['summary.json'])
assert summary['nativePlatform'] == 'linux' and summary['sourceWasUncommitted'] is True
for flag in ('nativeAudio', 'nativeWindowsReplayed', 'installedReplayed', 'productBinaryUploaded', 'fullProcessingQuality'):
    assert summary[flag] is False
assert summary['fullBuildExitCode'] == 0 and summary['duplicateBuildExitCode'] == 130
assert summary['abortedRegressionExitCode'] == 130 and summary['initialFullRegressionExitCode'] == 8
assert summary['initialFullRegressionPassed'] == 111 and summary['initialFullRegressionFailed'] == 1
assert summary['onlyFailure'] == 'desktop-stretch-ui' and summary['correctedUiExitCode'] == 0
assert summary['correctedUiChecks'] == 67 and summary['stretchStateChecks'] == 68
assert summary['stretchControllerChecks'] == 48 and summary['artifactChecks'] == 1045 and summary['callbackAudit'] == 0
initial = json.loads(entries['source-inputs-initial.json'])['inputs']
final = json.loads(entries['source-inputs.json'])['inputs']
assert len(initial) == len(final) == summary['sourceInputCount'] == 397 and initial.keys() == final.keys()
assert [name for name in final if final[name] != initial[name]] == summary['changedInputsForRerun'] == ['tests/stretch_ui_tests.cpp']
for digest in final.values():
    assert re.fullmatch('[0-9a-f]{64}', digest)
build = entries['logs/full-build.log'].decode()
assert [int(v) for v in re.findall(r'^\[(\d+)/384\]', build, re.M)] == list(range(1, 385))
full = entries['logs/full-tests.log'].decode()
outcomes = re.findall(r'^\s*\d+/112 Test\s+#(\d+):\s+(\S+).*?(Passed|\*\*\*Failed)', full, re.M)
assert len(outcomes) == 112 and len({row[0] for row in outcomes}) == 112
assert [row[1] for row in outcomes if row[2] != 'Passed'] == ['desktop-stretch-ui']
assert 'Exports inside a project must be in its exports directory' in full
assert 'stretch_state_checks=68' in full and 'schema=1.13' in full and 'stretch_controller_checks=48' in full
assert 'derived_shared_live_export_checks=1045 callback_audit=0' in full
worker = json.loads(next(line.split('5: ', 1)[1] for line in full.splitlines() if line.startswith('5: {') and 'actualOwnedCompletedJobs' in line))
assert worker == summary['worker'] and worker['checks'] == 241 and worker['actualOwnedCompletedJobs'] == 41
assert worker['unityShortCompletedJobs'] == 7 and worker['unityIntegerCopyExact'] is True
assert worker['unity384kDiscreteCopyExact'] is True and worker['nativeAudio'] is False
ui = entries['logs/corrected-ui-tests.log'].decode()
assert 'stretch_ui_checks=67' in ui and '100% tests passed, 0 tests failed out of 1' in ui
checks = json.loads(entries['extra-checks.json'])
assert len(checks) == 5 and all(row['status'] == 'fulfilled' and row['value']['exit_code'] == 0 for row in checks)
assert '46 native render receipt refusals' in checks[1]['value']['output']
assert '11 positive/refusal cases pass' in checks[4]['value']['output']
print('Retained local unity observations verified:111 regressions plus corrected67-check desktop workflow,68 state/48 controller/241 worker/1045 artifact checks. No native audio, Windows, installer or current package replay.')
