#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained installed Windows observations; no archived/native/audio replay."""
from pathlib import Path
import hashlib
import datetime
import io
import importlib.util
import json
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p / '.git').exists())
sys.path.insert(0, str(REPO / 'tests/results/M2/2026-10-10-native-restart-qualified'))
from capture_safety import check_member
spec = importlib.util.spec_from_file_location('sc_retained_input_bindings', REPO / 'tests/results/M2/2026-10-10-installed-linux-interruption/verify.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
reference_inputs = module.reference_inputs
sha = lambda b: hashlib.sha256(b).hexdigest()


def expected_payload():
    # Independently retained package receipt, before this interruption session.
    # Fixed identities prevent a reduced/rebound session receipt from qualifying
    # the operator's installed-payload loop. No product payload is loaded.
    prior = (REPO / 'tests/results/M2/2026-10-10-native-restart-qualified/capture.zip').read_bytes()
    assert sha(prior) == 'd44e7cc2c18feb57fa798d369df49fd2c0cc35daec218d73a32fe3a3b3eab5d4'
    check_member('capture.zip', prior)
    with zipfile.ZipFile(io.BytesIO(prior)) as z:
        receipt = z.read('package/receipt.json')
    assert sha(receipt) == '937b0ddc4d902a53dc6a3026a14e21c63ad0f36a9dc0bbc82a759e5930685d7b'
    payload = json.loads(receipt.decode('utf-8-sig'))['payload']
    assert len(payload) == 64
    return payload


def unpack(data):
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        assert len(z.namelist()) == len(set(z.namelist())) and z.testzip() is None
        result = {}
        for n in z.namelist():
            name = n.replace('\\', '/')
            assert name.count('/') >= 1
            name = name.split('/', 1)[1]
            assert name not in result
            result[name] = z.read(n)
        return result


def audit(root=ROOT):
    s = json.loads((root / 'summary.json').read_bytes())
    packed = (root / 'capture.zip').read_bytes()
    assert len(packed) == s['captureBytes'] and sha(packed) == s['captureSha256']
    check_member('capture.zip', packed)
    manifest = json.loads((root / 'capture-members.json').read_bytes())
    with zipfile.ZipFile(io.BytesIO(packed)) as z:
        assert len(z.namelist()) == len(set(z.namelist())) == len(manifest) == s['members']
        assert set(z.namelist()) == set(manifest) and z.testzip() is None
        b = {n: z.read(n) for n in z.namelist()}
    for n, data in b.items():
        assert manifest[n] == {'bytes': len(data), 'sha256': sha(data)}
        assert not data.startswith(b'\x7fELF') and not n.endswith('.iso')
    j = lambda data: json.loads(data.decode('utf-8-sig'))
    assert s['format'] == 'sc-installed-windows-interruption-evidence-v1'
    assert s['operatorFailuresPreserved'] == 3
    assert (s['parentExit'], s['helperExit'], s['freshExit']) == (1396899841, 1396899842, 0)
    assert s['installedSource'] == '4fb8c1cdf1b1365e5834256f4b39b51566dbed52'
    assert s['installedTree'] == 'd1b6be0bd9114af21d73a5dd6ba8172b53510c44'
    assert s['comparisonSource'] == 'db20b368035992f91b30fa3d9b2f8ac530239e16'
    binding = j(b['processing-input-binding.json'])
    assert binding['installedSource'] == s['installedSource'] and binding['comparedTo'] == s['comparisonSource']
    assert binding['files'] == reference_inputs(s['installedSource']) == reference_inputs(s['comparisonSource'])
    assert s['sequence'] == '20261010183111'
    assert s['guiSha256'] == '2302a5d5d2da63476a2f1f7074053b64d75a379d31973916bc35881ae29d57bd'
    assert s['helperSha256'] == 'ca352ca8325219b0dce6a61029878a76f85fc2cdd8d42d75f102b4ba248a2731'
    expected = ['20261010222200313', '20261010222419043', '20261010222736887', '20261010223018382']
    captures = [n for n in b if n.startswith('captures/')]
    assert len(captures) == 4
    payload = expected_payload()
    all_parents = set()
    for index, stamp in enumerate(expected):
        name = 'captures/installed-interruption-' + stamp + '.zip'
        files = unpack(b[name]); project = 'project-été-Κиїв/'
        events = j(files['records/interruption-' + stamp + '.json'])
        receipt = j(files['records/installed-receipt.json'])
        assert receipt['payload'] == payload
        event = lambda name: next(e['data'] for e in events if e['name'] == name)
        opened = files['records/project-restart-' + stamp + '-opened-source.json']
        assert opened == files[project + 'project.json']
        before = j(files['records/fixture-before.json'])
        assert {f['path'].replace('\\', '/') for f in before} == {'project.json', 'media/raw-été.wav'}
        for f in before:
            raw = files[project + f['path'].replace('\\', '/')]
            assert {'bytes': len(raw), 'sha256': sha(raw)} == {'bytes': f['bytes'], 'sha256': f['sha256']}
        identity = event('source-identity')
        assert identity['pid'] not in all_parents and identity['creationFileTime'] > 0
        all_parents.add(identity['pid'])
        assert identity['sha256'] == s['guiSha256'] and identity['exe'].endswith('\\soundcurrent-daw.exe')
        cleanup = event('cleanup')
        assert cleanup['workerThreadJoined'] is True
        assert all(p['actualExited'] is True for p in cleanup['processes'])
        assert len(cleanup['processes']) == (1 if index == 0 else 3 if index == 3 else 2)
        if index < 3:
            assert not any(e['name'] == 'completed' for e in events)
            failure = event('operator-failure')['message']
            marker = ['No unique actual control: stretchNumerator', 'cannot be read', 'Observed helper is not bound'][index]
            assert marker in failure
            assert all(p['exitCode'] in (1396899841, 1396899842) for p in cleanup['processes'])
            continue
        assert not any(e['name'] == 'operator-failure' for e in events)
        ordered = ['launch-source', 'opened-source', 'source-identity', 'helper-candidate',
                   'helper-observed', 'source-terminal', 'launch-recovery', 'opened-recovery',
                   'fresh-identity', 'inventory-active', 'active-refused', 'helper-terminal',
                   'threads-retired', 'incomplete-refused', 'final-recovery', 'quit-recovery',
                   'completed', 'cleanup']
        assert [e['name'] for e in events] == ordered
        times = [datetime.datetime.fromisoformat(e['utc'].replace('Z', '+00:00')) for e in events]
        assert all(t.tzinfo is not None and t.utcoffset() == datetime.timedelta(0) for t in times)
        assert all(a < c for a, c in zip(times, times[1:]))
        assert receipt['sourceHead'] == s['installedSource'] and receipt['sequence'] == s['sequence']
        assert receipt['payload']['soundcurrent-daw.exe']['sha256'] == s['guiSha256']
        assert receipt['payload']['sc-stretch-render-worker.exe']['sha256'] == s['helperSha256']
        for stage in ['source', 'recovery']:
            launch = event('launch-' + stage)
            assert launch['sessionId'] > 0 and launch['developerSdkUsed'] is False and launch['nativeAudioActivated'] is False
            assert launch['sha256'] == s['guiSha256']
            install = launch['exe'].rsplit('\\', 1)[0]
            for dll in ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll']:
                matches = [m for m in launch['modules'] if m['name'] == dll]
                assert len(matches) == 1 and matches[0]['path'].startswith(install + '\\')
        source, fresh = event('source-identity'), event('fresh-identity')
        helper = event('helper-observed'); candidate = event('helper-candidate')
        child = helper['identity']; op = helper['operation']; job = project + 'media/derived/' + op + '/'
        assert (source['pid'], child['pid'], fresh['pid']) == (s['sourcePid'], s['helperPid'], s['freshPid']) == (3012, 8304, 6448)
        assert source['creationFileTime'] < child['creationFileTime'] < fresh['creationFileTime']
        assert helper['parentPid'] == candidate['parentPid'] == source['pid'] == candidate['sourcePid']
        assert candidate['identity'] == child and candidate['oldSubstringMatched'] is False
        assert helper['argv'] == candidate['argv'] and len(helper['argv']) == 13
        norm = lambda p: p.replace('/', '\\').casefold()
        argv = helper['argv']
        assert norm(argv[0]) == norm(child['exe']) and argv[1] == '--project-root'
        assert norm(argv[2]) == norm(candidate['project'])
        assert argv[3] == '--jobs-root' and norm(argv[4]) == norm(candidate['project'] + '\\media\\derived')
        assert child['sha256'] == s['helperSha256']
        for e, original, code in [('source-terminal', source, 1396899841), ('helper-terminal', child, 1396899842)]:
            terminal = event(e); assert terminal['identity'] == original and terminal['actualExited'] is True and terminal['exitCode'] == code
        assert helper['threads'] and all(t['previousSuspendCount'] == 0 for t in helper['threads'])
        retired = event('threads-retired')
        assert retired['helperRunning'] is False and all(t['closed'] for t in retired['threads'])
        assert [{k: t[k] for k in ['tid', 'previousSuspendCount']} for t in retired['threads']] == helper['threads']
        active = event('active-refused'); incomplete = event('incomplete-refused')
        assert active['identity'] == child and active['running'] is True and active['selectedReviewEnabled'] is False
        assert incomplete['operation'] == op and incomplete['selectedReviewEnabled'] is False and incomplete['noFinalOrCompletion'] is True
        for stage, status in [('active', 'Renderer still owns this job'), ('incomplete', 'Incomplete render')]:
            tree = j(files['records/uia-restart-' + stamp + '-scan-' + stage + '.json'])
            items = [r for r in tree if r['type'] == 'ControlType.ListItem' and r['id'].endswith('.stretchRecoveryList')]
            buttons = [r for r in tree if r['id'].endswith('.reviewRetainedStretch') and r['class']]
            assert len(items) == len(buttons) == 1 and status in items[0]['name'] and buttons[0]['enabled'] is False
        partial = files[job + 'audio.partial']
        assert len(partial) == helper['partialBytes'] == incomplete['partialBytes'] == s['partialBytes'] == 4120
        assert sha(partial) == helper['partialSha256'] == active['partialSha256'] == incomplete['partialSha256'] == s['partialSha256']
        assert job + 'audio.wav' not in files and job + 'complete.json' not in files
        manifest_name = project + 'media/stretch-selections/' + op + '.json'
        selection, intent = j(files[manifest_name]), j(files[job + 'intent.json'])
        assert files[job + 'selection.sha256'] == sha(files[manifest_name]).encode()
        assert selection['operation'] == intent['operation'] == op and intent['complete'] is False
        req = selection['request']
        assert (req['timeNumerator'], req['timeDenominator'], req['pitchMilliCents']) == (3, 2, 0)
        assert req['contextEnabled'] is False and req['formantPreserved'] is False and 'warp' not in req
        raw = files[project + 'media/raw-été.wav']; stored = j(files[project + 'project.json'])
        assert raw[:4] == b'RIFF' and struct.unpack_from('<HHIIHH', raw, 20) == (3, 2, 48000, 384000, 8, 32)
        assert len(raw) == 44 + 1048576 * 8 and sha(raw) == req['sha256'] == intent['sourceSha256']
        assert intent['target'] == 1572864 and stored['tracks'][0]['clips'][0]['stretch'] is None
        selected = json.loads(selection['selection'])
        assert len(stored['tracks']) == len(stored['assets']) == len(stored['tracks'][0]['clips']) == 1
        assert stored['assets'][0]['sha256'] == sha(raw)
        assert stored['tracks'][0]['clips'][0]['assetId'] == req['assetId'] == stored['assets'][0]['id']
        assert stored['tracks'][0]['clips'][0]['lengthFrames'] == req['frames'] == req['sourceFrames'] == 1048576
        assert req['first'] == 0
        assert selected['assets'] == stored['assets'] and selected['projectId'] == stored['projectId']
        assert selected['tracks'][0]['clips'] == stored['tracks'][0]['clips']
        for phase in ['opened-source', 'opened-recovery', 'final-recovery']:
            assert files['records/project-restart-' + stamp + '-' + phase + '.json'] == files[project + 'project.json']
        after = j(files['records/fixture-after.json'])
        for f in after:
            data = files[project + f['path'].replace('\\', '/')]
            assert f['bytes'] == len(data) and f['sha256'] == sha(data)
        assert j(files['records/original-before.json']) == j(files['records/original-after.json'])
        quit = event('quit-recovery')
        assert quit['pid'] == fresh['pid'] and quit['normalCloseRequested'] is True and quit['actualExited'] is True and quit['forced'] is False and quit['exitCode'] == 0
        assert event('completed')['freshExit'] == 0 and event('completed')['projectAndRawUnchanged'] and event('completed')['originalUnchanged']
        assert [(p['identity'], p['exitCode']) for p in cleanup['processes']] == [(child, 1396899842), (fresh, 0), (source, 1396899841)]
    stop = j(b['host/host-stop.json'])
    assert stop['state'] == 'shut off' and stop['qemuPidGone'] and stop['receiverExit'] == 0
    assert stop['ownedCdEjected'] and stop['temporaryFirewallRuleRemoved'] and stop['otherVmsOff']
    disk = j(b['host/disk-info-offline.json'])
    assert disk['format'] == 'qcow2' and 'backing-filename' not in disk and disk['format-specific']['data']['corrupt'] is False
    assert s['actualInstalledGuiQualified'] and s['statuses'] == ['Active', 'Incomplete']
    for field in ['separateHelperDeath', 'savedProjectAndRawUnchanged', 'originalProvenFixtureUnchanged', 'vmOff']:
        assert s[field] is True
    for field in ['audioPlaybackRequested', 'automaticHelperContainment', 'powerLossQualified', 'preAcknowledgementGuiQualified', 'completedOrphanGuiQualified', 'largeInventoryQualified', 'processingQualityQualified', 'fullGoalComplete', 'productUploaded']:
        assert s[field] is False
    return s


if __name__ == '__main__':
    audit()
    print('Installed Windows GUI interruption, UI Automation properties and retained failures audited; no native/code/audio replay')
