#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse misleading installed-preview claims without activating audio."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from verify_windows_installed_preview import (
    verify_source_inputs, verify_native_modules, verify_observer_failure,
)


def refused(operation, message):
    try:
        operation()
    except ValueError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError('Misleading evidence accepted: ' + message)


def run():
    receipt = json.loads((ROOT / 'tests/results/X007/2026-10-08-windows-installed-preview.json').read_text())
    with zipfile.ZipFile(ROOT / 'tests/results/X007' / receipt['capsule']['name']) as z:
        def read(name):
            return json.loads(z.read(name).decode('utf-8-sig'))
        manifest = read('prepared/native-source-audited-files.json')
        native = read('installed-native-module-proof/result.json')
        prepared = read('prepared/receipt.json')
        tools = read('prepared/test-only-tools.json')
        installed = read('initial-installed-observer-failure/result.json')
        fault_name = next(n for n in z.namelist() if n.startswith('initial-installed-observer-failure/') and n.endswith('first-fault.json'))
        fault = read(fault_name)
        journal = read(str(Path(fault_name).parent / 'journal.json'))
        raw_name = next(n for n in z.namelist() if n.startswith('initial-installed-observer-failure/Installed workflow - Ελληνικά/media/') and n.endswith('journal.json'))
        raw = read(raw_name)
        stderr = z.read('initial-installed-observer-failure/native-workflow.stderr').decode('utf-8-sig')
    verify_source_inputs(receipt['sourceHead'], manifest)
    verify_native_modules(native, prepared, tools)
    verify_observer_failure(installed, fault, journal, raw, stderr, receipt['initialFailure'])
    changed = deepcopy(manifest)
    changed['files'][0]['sha256'] = '0' * 64
    refused(lambda: verify_source_inputs(receipt['sourceHead'], changed), 'differs from frozen Git source')
    for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll'):
        changed = deepcopy(native)
        next(m for m in changed['fixtureModules'] if m['name'].casefold() == name.casefold())['path'] = 'C:\\DeveloperSDK\\' + name
        refused(lambda: verify_native_modules(changed, prepared, tools), 'non-installed dependency')
    changed = deepcopy(native)
    changed['fixtureSha256'] = '0' * 64
    refused(lambda: verify_native_modules(changed, prepared, tools), 'differs from qualified test tool')
    changed = deepcopy(fault)
    changed['fault']['rejected']['discontinuity'] = False
    refused(lambda: verify_observer_failure(installed, changed, journal, raw, stderr, receipt['initialFailure']), 'discontinuity details differ')
    changed = deepcopy(fault)
    changed['fault']['rejected']['position'] = 288000
    refused(lambda: verify_observer_failure(installed, changed, journal, raw, stderr, receipt['initialFailure']), 'discontinuity details differ')
    refused(lambda: verify_observer_failure(installed, fault, journal, raw, 'Route configuration failed', receipt['initialFailure']), 'Initial observer failure was lost')
    print('Installed preview evidence: 3 positive controls and 10 misleading-claim refusals passed')


if __name__ == '__main__':
    run()
