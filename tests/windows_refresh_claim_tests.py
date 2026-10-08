#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prevent installed UI acceptance from hiding native workflow failures."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from verify_windows_installer_refresh import execution


def refused(result, prepared, tools):
    try: execution(result, prepared, tools)
    except ValueError: return
    raise AssertionError('Misleading installed workflow claim accepted')


def run():
    with zipfile.ZipFile(ROOT/'tests/results/X007/2026-10-08-windows-installer-refresh.zip') as z:
        read = lambda n: json.loads(z.read(n).decode('utf-8-sig'))
        result = read('attempt-1/result.json')
        prepared, tools = read('prepared/receipt.json'), read('prepared/test-tools.json')
    execution(result, prepared, tools)
    count = 0
    for name in ('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','sndfile.dll','qwindows.dll'):
        changed = deepcopy(result)
        next(m for m in changed['fixtureModules'] if m['name'].casefold() == name.casefold())['path'] = 'C:\\SDK\\'+name
        refused(changed, prepared, tools); count += 1
    for key, value in (('exitCode',0),('nativeWorkflowExit',0),('fixtureSha256','0'*64),
                       ('compilerOnPath',True),('qtSdkOnPath',True),('sessionId',0),
                       ('payloadVerified',False),('sourceHead','0'*40)):
        changed = deepcopy(result); changed[key] = value
        refused(changed, prepared, tools); count += 1
    print(f'Installed refresh: original failure control and {count} misleading-claim refusals passed')


if __name__ == '__main__': run()
