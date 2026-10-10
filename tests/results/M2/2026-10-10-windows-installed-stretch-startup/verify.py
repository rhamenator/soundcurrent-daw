# SPDX-License-Identifier: GPL-3.0-only
"""Check retained installer/startup observations; never executes a guest or setup."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--receipt', type=Path, default=Path(__file__).with_name('qualification.json'))
args = parser.parse_args()
r = json.loads(args.receipt.read_bytes())
sha = lambda b: hashlib.sha256(b).hexdigest()
source = 'f9a63532685c988b4d7203da537957cab41dff92'
installer = '75af398f0c2cbbc36c0f236311745e89f34321ef59e9cde149da7d80dbefe302'
uuid = '670168F5-4E3F-4E7F-B218-3583DB31F0F8'
assert r['format'] == 'sc-windows-installed-startup-capture-v1'
assert r['sourceCommit'] == source
assert r['sourceTree'] == '742b786db890a67bbefddcdac8bdb6125d2a481e'
assert r['installerSha256'] == installer
assert r['setupExit'] == r['normalMainExit'] == 0
assert r['payloadFilesVerified'] == 64 and r['mainPid'] == 5308
assert r['installationStartupQualified'] is True and r['vmStopped'] is True
for key in ('fullStretchAcceptance', 'nativeAudioActivated', 'releaseUploaded', 'fullParity'):
    assert r[key] is False, key
archive = args.receipt.parent / 'capture.zip'
assert archive.stat().st_size == r['archive']['bytes'] < 2 * 1024 * 1024
assert sha(archive.read_bytes()) == r['archive']['sha256']
with zipfile.ZipFile(archive) as z:
    infos = z.infolist()
    assert len(infos) == len({i.filename for i in infos}) == 30
    assert set(z.namelist()) == set(r['archive']['entries'])
    assert sum(i.file_size for i in infos) < 4 * 1024 * 1024
    assert z.testzip() is None
    data = {}
    for i in infos:
        p = PurePosixPath(i.filename)
        assert not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename
        assert not i.is_dir() and ((i.external_attr >> 16) & 0o170000) != 0o120000
        assert p.name != 'private-transport.json' and p.suffix not in ('.exe', '.dll')
        b = z.read(i)
        assert r['archive']['entries'][i.filename] == {'bytes': len(b), 'sha256': sha(b)}
        data[i.filename] = b
load = lambda name: json.loads(data[name].decode('utf-8-sig'))
# The first deadline was a harness observation, not an actual installer exit.
first = load('first/setup-result.json')
assert first == load('installed/setup-result.json')
assert first['setupPid'] == 7864 and 'setupExit' not in first
assert first['runtimeBefore'] is None and first['exitCode'] == 1
assert first['error'] == 'Setup observation deadline; retain live process for diagnosis'
retry = load('installed/retry-setup-result.json')
actual = load('installed/installed-verification.json')
for report in (first, retry, actual):
    assert report['sourceHead'] == source and report['installerSha256'] == installer
    assert report['vmUuid'] == uuid and report['sessionId'] == 1
    assert report['compilerOnPath'] is False and report['qtSdkOnPath'] is False
    assert report['nativeAudioActivated'] is False
assert retry['setupExit'] == actual['setupExit'] == 0
assert retry['setupPid'] == actual['setupPid'] == 4704
assert retry['verifiedPayloadFiles'] == actual['verifiedPayloadFiles'] == 64
assert retry['error'] == 'Shortcut missing' and retry['exitCode'] == 1
assert actual['fullStretchAcceptance'] is False
assert actual['runtimeAfter'] == retry['runtimeAfter'] == {'Installed': 1, 'Version': 'v14.44.35211.00'}
assert sha(data['installed/receipt.json']) == '39c5032925333a64ec078b15ede7b694b5c9d51b2321936e3b5b0457fbebb718'
assert data['first/receipt.json'] == data['installed/receipt.json']
package = load('installed/receipt.json')
assert package['sourceHead'] == source and len(package['payload']) == 64
for key in ('cleanInstallQualified', 'nativeAudioReplayed', 'releaseUploaded'):
    assert package[key] is False
slot = retry['installLocation']
exe = slot + '\\soundcurrent-daw.exe'
assert len(actual['shortcuts']) == 2
assert all(link['target'] == exe for link in actual['shortcuts'])
assert all(link['path'].endswith('SoundCurrent DAW Preview 20261009232400-f9a63532685c.lnk') for link in actual['shortcuts'])
assert len({link['path'] for link in actual['shortcuts']}) == 2
assert actual['registration'].endswith('Uninstall\\SoundCurrentDAW-20261009232400-f9a63532685c')
main = load('installed/main-20261010002451001.json')
assert main['sourceHead'] == source and main['pid'] == r['mainPid']
assert data['installed/main-pid.txt'].decode() == str(main['pid'])
assert main['sessionId'] == 1 and main['windowHandle'] > 0
assert main['exe'] == exe and main['title'] == 'SoundCurrent DAW'
assert main['exeSha256'] == package['payload']['soundcurrent-daw.exe']['sha256']
assert main['closed'] is True and main['exitCode'] == 0
assert main['developerSdkUsed'] is False and main['nativeAudioActivated'] is False
for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll'):
    found = [m for m in main['modules'] if m['name'] == name]
    assert len(found) == 1
    relative = 'platforms\\qwindows.dll' if name == 'qwindows.dll' else name
    assert found[0]['path'].lower() == (slot + '\\' + relative).lower()
assert sha(data['installed/project-été-Κиїв/project.json']) == 'f9ae3bf852f2c2a9e8bd95414afc75cd0bcf665e6383ea33988f19cc2e460c29'
assert sha(data['installed/project-été-Κиїв/media/raw-été.wav']) == '6e22cfd507c93dc03681618c2e3a1e2970d9547263f148c10b76c0c3b71bd2b2'
actions = load('observations/actions.json')
assert actions['mainPid'] == main['pid'] and actions['setupPid'] == actual['setupPid']
for key in ('uacConsentObserved', 'openedOwnedUnicodeProject', 'projectBytesUnchanged', 'normalWindowClose'):
    assert actions[key] is True
for key in ('stretchRendered', 'audioPlaybackStarted', 'exportsCreated', 'uninstallReinstallTested'):
    assert actions[key] is False
cleanup = load('observations/cleanup.json')
for key in ('allVmsOff', 'temporaryFirewallRuleRemoved', 'receiverPortClosed', 'originalTemplateUnchanged'):
    assert cleanup[key] is True
print('Retained installed Windows startup verified: manifest30, original failure scope, actual setup exit/payload/shortcuts, installed module paths, unchanged owned Unicode project and normal main exit. No installer, render or audio replay.')
