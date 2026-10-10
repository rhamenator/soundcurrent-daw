# SPDX-License-Identifier: GPL-3.0-only
"""Check retained installed workflow observations, never replay a VM or installer."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, math, struct, zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--receipt', type=Path, default=Path(__file__).with_name('qualification.json'))
args = parser.parse_args()
r = json.loads(args.receipt.read_bytes())
sha = lambda b: hashlib.sha256(b).hexdigest()
source = 'f9a63532685c988b4d7203da537957cab41dff92'
raw_sha = '6e22cfd507c93dc03681618c2e3a1e2970d9547263f148c10b76c0c3b71bd2b2'
export_sha = 'cf7378b79ee37642b0a8591ecc5221a6b57f9f83915820206ef461a78a4dd76c'
folder = 'project-été-Κиїв/'
assert r['format'] == 'sc-installed-windows-stretch-workflow-v1'
assert r['sourceCommit'] == source
assert r['sourceTree'] == '742b786db890a67bbefddcdac8bdb6125d2a481e'
assert r['installerSha256'] == '75af398f0c2cbbc36c0f236311745e89f34321ef59e9cde149da7d80dbefe302'
assert r['positionedStretchWorkflowQualified'] is True
assert r['removalReinstallationQualified'] is True
assert r['mainPids'] == [9136, 1880, 1880] and r['helperPid'] == 2236
assert r['normalMainExits'] == [0, 0, 0]
for key in ('nativeAudioActivated', 'fullProcessingQuality', 'releaseUploaded', 'fullParity'):
    assert r[key] is False, key
archive = args.receipt.parent / 'capture.zip'
assert archive.stat().st_size == r['archive']['bytes'] < 3 * 1024 * 1024
assert sha(archive.read_bytes()) == r['archive']['sha256']
with zipfile.ZipFile(archive) as z:
    infos = z.infolist()
    assert len(infos) == len({i.filename for i in infos}) == 59
    assert set(z.namelist()) == set(r['archive']['entries'])
    assert sum(i.file_size for i in infos) < 4 * 1024 * 1024 and z.testzip() is None
    data = {}
    for i in infos:
        p = PurePosixPath(i.filename)
        assert not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename
        assert not i.is_dir() and ((i.external_attr >> 16) & 0o170000) != 0o120000
        assert p.name != 'private-transport.json' and p.suffix not in ('.exe', '.dll')
        b = z.read(i)
        assert r['archive']['entries'][i.filename] == {'bytes': len(b), 'sha256': sha(b)}
        data[i.filename] = b
load = lambda n: json.loads(data[n].decode('utf-8-sig'))
package = load('first/receipt.json')
# This earlier builder receipt is immutable; later installed acceptance is separate.
assert sha(data['first/receipt.json']) == '39c5032925333a64ec078b15ede7b694b5c9d51b2321936e3b5b0457fbebb718'
assert package['sourceHead'] == source and len(package['payload']) == 64
for key in ('cleanInstallQualified', 'nativeAudioReplayed', 'releaseUploaded'):
    assert package[key] is False
slot = 'C:\\Users\\copperfin\\AppData\\Local\\Programs\\SoundCurrent DAW Preview\\20261009232400-f9a63532685c'
for prefix, stamp, pid in [('first', '20261010003809896', 9136),
                           ('second', '20261010004655681', 1880),
                           ('cycle', '20261010005338195', 1880)]:
    m = load(prefix + '/main-' + stamp + '.json')
    assert m['sourceHead'] == source and m['pid'] == pid
    assert m['sessionId'] == 1 and m['windowHandle'] > 0
    assert m['exe'] == slot + '\\soundcurrent-daw.exe' and m['title'] == 'SoundCurrent DAW'
    assert m['exeSha256'] == package['payload']['soundcurrent-daw.exe']['sha256']
    assert m['closed'] is True and m['exitCode'] == 0
    assert m['developerSdkUsed'] is False and m['nativeAudioActivated'] is False
    for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'sndfile.dll', 'qwindows.dll'):
        found = [v for v in m['modules'] if v['name'] == name]
        relative = 'platforms\\qwindows.dll' if name == 'qwindows.dll' else name
        assert len(found) == 1 and found[0]['path'].lower() == (slot + '\\' + relative).lower()
observer = load('first/helper-observer.json')
assert observer['stopped'] is True and observer['pollMilliseconds'] == 25
assert observer['traceRegistered'] is False and observer['traceError'].strip() == 'Access denied'
assert observer['startEvents'] == [] and len(observer['processes']) == 1
h = observer['processes'][0]
assert h['pid'] == 2236 and h['sessionId'] == 1
assert h['path'] == slot + '\\sc-stretch-render-worker.exe'
assert h['sha256'] == package['payload']['sc-stretch-render-worker.exe']['sha256']
assert next(m['path'] for m in h['modules'] if m['name'] == 'sndfile.dll') == slot + '\\sndfile.dll'
actions = load('observations/actions.json')
assert actions['renderMainPid'] == 9136 and actions['helperPid'] == 2236
assert actions['reopenMainPid'] == actions['reinstallMainPid'] == 1880
for key in ('renderVerifiedBeforeExplicitApply', 'undoRedoSaveObserved',
            'newProcessReopenedSavedProject', 'twoActualWavExports',
            'explicitOwnedExportReplacement', 'normalWindowCloseObserved', 'postReinstallStartupOnly'):
    assert actions[key] is True
assert actions['nativeAudioActivated'] is False and actions['fullParity'] is False
state = load('first/state-actions.json')
assert state['pid'] == 9136 and state['sessionId'] == 1
assert state['undoSaved'] is True and state['redoSaved'] is True
initial = load('initial/project.json')
assert sha(data['initial/project.json']) == 'f9ae3bf852f2c2a9e8bd95414afc75cd0bcf665e6383ea33988f19cc2e460c29'
assert load('first/undone-project.json') == initial
p = load('first/' + folder + 'project.json')
assert p == load('first/applied-project.json') == load('first/redone-project.json')
assert len(p['assets']) == 2 and len(p['tracks']) == 1
assert p['sampleRate'] == 48000 and p['exportRange'] == {'startFrame': 0, 'endFrame': 12288}
clip = p['tracks'][0]['clips'][0]
assert clip['lengthFrames'] == 12288 and clip['sourceFrame'] == clip['startFrame'] == 0
stretch = clip['stretch']
assert stretch['sourceOrigin'] == {'algorithm': 'soundcurrent.src-positioned-best-v1', 'frame': 17, 'fraction': 1, 'denominator': 2}
assert stretch['sourceFrames'] == 8192 and stretch['sourceSha256'] == raw_sha
assert stretch['settings'] == {'timeNumerator': 3, 'timeDenominator': 2, 'pitchMilliCents': 700007, 'formantPreserved': True}
assert stretch['processor'] == 'soundcurrent.stretch-rubberband4-r3-positioned-v2'
assert stretch['sourceAssetId'] == p['assets'][0]['id'] and clip['assetId'] == p['assets'][1]['id']
# The recorded UI parameters must agree with the adopted source/processor state.
# JSON booleans are not integer timing or pitch values.
origin = actions['selectedExactSourceOrigin']
assert set(origin) == {'frame', 'fraction', 'denominator'}
for key, value in origin.items():
    assert type(value) is int and value == stretch['sourceOrigin'][key]
assert type(actions['sourceFrames']) is int and actions['sourceFrames'] == stretch['sourceFrames']
for key in ('timeNumerator', 'timeDenominator', 'pitchMilliCents'):
    assert type(actions[key]) is int and actions[key] == stretch['settings'][key]
assert actions['formantPreserved'] is stretch['settings']['formantPreserved']
for prefix in ('second', 'cycle'):
    for n in data:
        if n.startswith('first/' + folder):
            relative = n[len('first/'):]
            assert data[n] == data[prefix + '/' + relative], relative
    assert data[prefix + '/project-été-Κиїв-mix.wav'] == data['first/project-été-Κиїв-mix.wav']
assert data['second/before-reopen-project.json'] == data['first/' + folder + 'project.json']

def wave(b):
    """Independent bounded decoder for retained float RIFF/RF64, no audio library."""
    assert b[:4] in (b'RIFF', b'RF64') and b[8:12] == b'WAVE'
    o = 12; fmt = pcm = ds = fact = None
    while o + 8 <= len(b):
        tag, n = struct.unpack_from('<4sI', b, o); o += 8
        if tag == b'ds64':
            assert n >= 28
            ds = struct.unpack_from('<QQQ', b, o)
            assert ds[0] == len(b) - 8
        if tag == b'data' and n == 0xffffffff:
            assert ds is not None; n = ds[1]
        assert o + n <= len(b)
        v = b[o:o + n]
        if tag == b'fmt ':
            assert fmt is None
            fmt = struct.unpack_from('<HHIIHH', v)
            assert fmt[1:] == (2, 48000, 384000, 8, 32)
            if fmt[0] == 0xfffe:
                assert len(v) == 40 and struct.unpack_from('<HHI', v, 16) == (22, 32, 3)
                assert v[24:40] == bytes.fromhex('0300000000001000800000aa00389b71')
            else:
                assert fmt[0] == 3
        if tag == b'fact':
            fact = struct.unpack_from('<I', v)[0]
        if tag == b'data':
            assert pcm is None; pcm = v
        o += n + n % 2
    assert o == len(b) and fmt and pcm is not None and len(pcm) % fmt[4] == 0
    frames = len(pcm) // fmt[4]
    if b[:4] == b'RIFF':
        assert struct.unpack_from('<I', b, 4)[0] == len(b) - 8
    else:
        assert ds and ds[1] == len(pcm) and ds[2] == frames
    if fact is not None:
        assert fact == frames
    samples = struct.unpack('<' + 'f' * (len(pcm) // 4), pcm)
    assert all(math.isfinite(v) for v in samples)
    return frames, max(abs(v) for v in samples), pcm

raw = data['first/' + folder + 'media/raw-été.wav']
assert sha(raw) == raw_sha and wave(raw)[0] == 12000
asset = p['assets'][1]
derived_path = folder + asset['path']
derived = data['first/' + derived_path]
export = data['first/project-été-Κиїв-mix.wav']
df, peak, dp = wave(derived); ef, epk, ep = wave(export)
assert df == ef == asset['frames'] == 12288 and peak == epk == 2.226808547973633
assert peak > 1 and dp == ep and sha(export) == export_sha and len(export) == 98392
assert sha(derived) == asset['sha256'] == 'a1545dde204ab2cdfbc642d31a8325fd5ab8a5a945abd23eab629552141a44bf'
marker = load('first/' + derived_path.replace('audio.wav', 'complete.json'))
assert marker['complete'] is True and marker['writtenFrames'] == marker['target'] == 12288
assert marker['audioSha256'] == sha(derived) and marker['sampleSha256'] == sha(dp)
assert marker['peakLinear'] == peak and marker['renderKey'] == stretch['renderKey']
assert marker['processor'] == stretch['processor'] and marker['sourceSha256'] == raw_sha
assert marker['first'] == 17 and marker['firstFraction'] == 1 and marker['firstDenominator'] == 2
assert marker['pitchMilliCents'] == 700007 and marker['formantPreserved'] is True
cycle = load('cycle/cycle-result.json')
assert cycle['sourceHead'] == source and cycle['installedWorkflowClaimed'] is False
assert cycle['exitCode'] == cycle['uninstallExit'] == cycle['reinstallExit'] == 0
assert cycle['uninstallPid'] == 6108 and cycle['reinstallPid'] == 8856
assert cycle['removalPreserved'] is True and cycle['reinstallPreserved'] is True
assert cycle['dataBefore'] == cycle['dataAfter']
files = json.loads(cycle['dataBefore'])
assert len(files) == 8 and len({f['path'] for f in files}) == 8
for f in files:
    relative = f['path'].split('\\project-été-Κиїв\\', 1)[1].replace('\\', '/')
    b = data['cycle/' + folder + relative]
    assert f['bytes'] == len(b) and f['sha256'] == sha(b)
cleanup = load('observations/cleanup.json')
for key in ('allVmsOff', 'temporaryFirewallRuleRemoved', 'receiverPortClosed', 'originalTemplateUnchanged'):
    assert cleanup[key] is True
assert cleanup['receiverExit'] == 143 and cleanup['releaseUploaded'] is False
print('Retained installed Windows stretch workflow verified: manifest59, installed main/helper identity, Apply/Undo/Redo/save/reopen, exact repeat WAV/derived PCM and float headroom, removal/reinstallation preservation. No native replay or full quality claim.')
