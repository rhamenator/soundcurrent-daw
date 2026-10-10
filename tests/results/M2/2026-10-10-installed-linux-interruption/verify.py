#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained installed GUI evidence; execute no archived program or audio."""
from pathlib import Path
import hashlib
import io
import json
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / '2026-10-10-native-restart-qualified'))
from capture_safety import check_member

sha = lambda b: hashlib.sha256(b).hexdigest()


def verify(root=ROOT):
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
        assert not data.startswith(b'\x7fELF') and not n.endswith(('.deb', '.pyc'))
    j = lambda n: json.loads(b[n])
    w = 'workspace/'
    p = w + 'project-été-Κиїв/'
    assert s['format'] == 'sc-installed-linux-interruption-evidence-v1'
    assert s['installedSource'] == '5300010ab521917027501c831b550092e535c210'
    assert s['installedTree'] == '83ff01f11fa64c32c424af8d5ed36091dfdbd37c'
    assert s['version'] == '0.1.0~preview.20261010165009.5300010ab521'
    assert s['packageSha256'] == 'dc26d1411f7d00869a9f72369d08de3aa5a2ee02ed0faf7025033020b73b43d0'
    assert s['guiSha256'] == '1a6fdd8eb2060039a884c79860d27d3f98d733b46b4e8ede4667f914592fc8b3'
    assert s['helperSha256'] == '5ce259bb47f82400a2218e6d297b198aaa87bc65a58ca2e7aa02cedfb262fae7'
    assert b[w + 'installed-version.txt'].decode().strip() == s['version']
    hashes = dict(line.split(None, 1)[::-1] for line in b[w + 'installed-binaries.sha256'].decode().splitlines())
    assert hashes == {'/usr/bin/soundcurrent-daw': s['guiSha256'], '/usr/bin/sc-stretch-render-worker': s['helperSha256']}
    package = j(w + 'package-receipt.json')
    assert package['source_head'] == s['installedSource'] and package['source_tree'] == s['installedTree']
    assert package['packaged_executable_sha256'] == s['guiSha256']
    assert package['artifacts']['soundcurrent-daw_' + s['version'] + '_amd64.deb']['sha256'] == s['packageSha256']
    assert b[w + 'developer-tools.txt'] == b''
    assert "not Path('/dev/snd').exists()" in b[w + 'manager.py'].decode()
    assert b'for program in g++ clang++ cmake qmake6' in b[w + 'root-launch.sh']
    assert b'failed to exec' in b['initial-runtime-python-absent/container.log']
    assert j('initial-runtime-python-absent/container-exit.json')['exit'] == 1
    assert j('second-runtime-install-order-timezone/container-exit.json')['exit'] == 1
    assert b'Device or resource busy' in b['second-runtime-install-order-timezone/python-install.log']
    assert j('container-exit.json')['exit'] == 0
    command = j('container-command.json')['command']
    for flag in ['--private-network', '--volatile=overlay', '--register=no', '--timezone=off']:
        assert flag in command
    assert len([x for x in command if x.startswith('--bind=')]) == 1
    for action in ['open-stretch', 'render-interrupt', 'kill-parent', 'open-recovery', 'scan-ready',
                   'select-active', 'kill-helper', 'scan-incomplete', 'quit-recovery']:
        receipt = j(action + '-command.json')
        assert receipt['exit_code'] == 0
        for flag in ['--mount', '--pid', '--net', '--ipc', '--uts', '--no-new-privs']:
            assert flag in receipt['command']
    for n in ['user-process-status.txt', 'app-source-status.txt', 'app-recovery-status.txt', 'helper-status.txt']:
        fields = dict(line.split(':', 1) for line in b[w + n].decode().splitlines() if ':' in line)
        assert fields['Uid'].split() == ['1000'] * 4
        assert int(fields['CapEff'].strip(), 16) == 0 and fields['NoNewPrivs'].strip() == '1'
    parent, fresh, helper = [j(w + n + '.json') for n in ['app-source', 'app-recovery', 'helper-observed']]
    assert len({parent['pid'], fresh['pid'], helper['pid']}) == 3
    assert fresh['startTicks'] > parent['startTicks'] > 0
    for app in [parent, fresh]:
        assert app['exe'] == '/usr/bin/soundcurrent-daw'
        assert app['cmdline'] == ['/usr/bin/soundcurrent-daw', '--language', 'en', '/home/preview/project-été-Κиїв']
    assert helper['exe'] == '/usr/bin/sc-stretch-render-worker' and helper['parentPid'] == parent['pid']
    assert helper['startTicks'] > parent['startTicks']
    assert helper['cmdline'][1:5] == ['--project-root', parent['cmdline'][-1], '--jobs-root', parent['cmdline'][-1] + '/media/derived']
    stopped = helper['stoppedIdentity']
    assert stopped['state'] == 'T'
    for key in ['pid', 'startTicks', 'exe', 'cmdline', 'parentPid']:
        assert stopped[key] == helper[key]
    for kind, original in [('parent', parent), ('helper', helper)]:
        signal = j(w + kind + '-signal.json')
        assert signal['signal'] == 'SIGKILL' and signal['requestedOnly'] is True
        for key in ['pid', 'startTicks', 'exe', 'cmdline']:
            assert signal['identity'][key] == original[key]
    active = j(w + 'active-inspection.json')
    assert active['state'] == 'T' and active['helperPid'] == helper['pid']
    assert active['startTicks'] == helper['startTicks'] and active['parentPid'] == parent['parentPid']
    terminal = j(w + 'helper-exit.json')
    assert terminal == {'pid': helper['pid'], 'waitStatus': 9, 'exitCode': -9}
    assert j(w + 'app-source-exit.json') == {'pid': parent['pid'], 'exitCode': -9}
    assert j(w + 'app-recovery-exit.json') == {'pid': fresh['pid'], 'exitCode': 0}
    cleanup = j(w + 'manager-cleanup.json')
    assert cleanup[:2] == [{'kind': 'app', 'pid': parent['pid'], 'exitCode': -9}, {'kind': 'app', 'pid': fresh['pid'], 'exitCode': 0}]
    assert len(cleanup) == 4 and all(x['kind'] == 'service' and x['exitCode'] == 0 for x in cleanup[2:])
    before, after = j(w + 'fixture-before.json'), j(w + 'fixture-after.json')
    assert before == after and set(before) == {'.save.lock', 'project.json', 'media/raw-été.wav'}
    for n, d in before.items():
        assert {'bytes': len(b[p + n]), 'sha256': sha(b[p + n])} == d
    project = j(p + 'project.json')
    assert len(project['tracks']) == len(project['assets']) == 1
    clip = project['tracks'][0]['clips'][0]
    assert clip['stretch'] is None and clip['lengthFrames'] == 1048576
    assert project['assets'][0]['sha256'] == before['media/raw-été.wav']['sha256']
    raw = b[p + 'media/raw-été.wav']
    assert raw[:12] == b'RIFF' + struct.pack('<I', len(raw) - 8) + b'WAVE'
    assert struct.unpack_from('<HHIIHH', raw, 20) == (3, 2, 48000, 384000, 8, 32)
    assert len(raw) == 44 + 1048576 * 8
    op = s['operation']
    job = p + 'media/derived/' + op + '/'
    assert helper['job'] == '/home/preview/project-été-Κиїв/media/derived/' + op
    assert job + 'audio.wav' not in b and job + 'complete.json' not in b
    partial = b[job + 'audio.partial']
    assert len(partial) == helper['partialBytes'] == s['partialBytes'] > 4096
    assert sha(partial) == helper['partialSha256'] == s['partialSha256'] == active['partialSha256']
    assert active['partialBytes'] == len(partial) and active['noComplete']
    selection_name = p + 'media/stretch-selections/' + op + '.json'
    selection, intent = j(selection_name), j(job + 'intent.json')
    assert b[job + 'selection.sha256'] == sha(b[selection_name]).encode()
    assert b[job + 'start.request'] == b'start'
    assert selection['operation'] == intent['operation'] == op and intent['complete'] is False
    request = selection['request']
    assert (request['timeNumerator'], request['timeDenominator'], request['pitchMilliCents']) == (3, 2, 0)
    assert request['contextEnabled'] is False and 'warp' not in request and request['formantPreserved'] is False
    assert intent['target'] == 1572864 and intent['sourceFrames'] == 1048576
    assert intent['sourceSha256'] == request['sha256'] == sha(raw)
    selected = json.loads(selection['selection'])
    assert selected['projectId'] == project['projectId'] and selected['assets'] == project['assets']
    assert selected['tracks'][0]['id'] == project['tracks'][0]['id'] and selected['tracks'][0]['clips'] == [clip]
    nodes = lambda n: sorted((x['id'], x['info']['props']['node.name']) for x in j(w + n) if x['type'] == 'PipeWire:Interface:Node')
    assert nodes('graph-before.json') == nodes('graph-after.json')
    assert {name for _, name in nodes('graph-after.json')} == {'sc-preview-source', 'sc-preview-sink', 'Dummy-Driver', 'Freewheel-Driver'}
    assert j(w + 'session-complete.json') == {'apps': 2, 'helperReaped': True, 'installedGuiInterruption': True, 'audioEndpointActivated': False, 'powerLossQualified': False}
    for image in ['selected-active.png', 'selected-incomplete.png', 'frozen-helper-source.png']:
        assert b[w + image].startswith(b'\x89PNG\r\n\x1a\n')
    bindings = j(w + 'processing-input-binding.json')
    assert bindings['installedSource'] == s['installedSource'] and bindings['comparedTo'] == 'b525b81c3cbe69cd8325fcaa6843df64913a14c7'
    assert {'ui/stretch_controller.cpp', 'ui/studio_window.cpp', 'tools/stretch_render_worker.cpp'} <= set(bindings['files'])
    assert s['recoveryStatuses'] == ['Active', 'Incomplete'] and s['separateHelperDeath']
    assert (s['parentExitCode'], s['helperExitCode'], s['recoveryExitCode']) == (-9, -9, 0)
    for key in ['installedGuiPartialInterruptionQualified', 'savedProjectAndRawUnchanged', 'noAttachment']:
        assert s[key] is True
    for key in ['automaticHelperContainment', 'audioEndpointActivated', 'physicalOsQualified', 'windowsInstalledGuiQualified', 'preAcknowledgementGuiQualified', 'completedOrphanGuiQualified', 'powerLossQualified', 'largeInventoryQualified', 'processingQualityQualified', 'fullGoalComplete', 'productUploaded']:
        assert s[key] is False
    return s


if __name__ == '__main__':
    verify()
    print('Installed Linux GUI interruption capture audited; screenshot observations remain manual; no native/code/audio replay')
