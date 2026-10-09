# SPDX-License-Identifier: GPL-3.0-only
"""Verify retained native observations; does not execute Windows or an installer."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, math, struct, sys, zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--receipt', type=Path, default=Path(__file__).with_name('qualification.json'))
parser.add_argument('--check-current-inputs', action='store_true')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import package_windows_preview as gate

r = json.loads(args.receipt.read_text())
assert r['format'] == 'sc-windows-stretch-preview-capture-v1'
assert r['sourceCommit'] == 'f9a63532685c988b4d7203da537957cab41dff92'
assert r['sourceTree'] == '742b786db890a67bbefddcdac8bdb6125d2a481e'
assert r['nativePlatform'] == 'win32' and r['vmStopped'] is True
for k in ('cleanInstallQualified', 'nativeAudioActivated', 'releaseUploaded'):
    assert r[k] is False, k
archive = args.receipt.parent / 'capture.zip'
sha = lambda b: hashlib.sha256(b).hexdigest()
assert archive.stat().st_size == r['archive']['bytes']
assert sha(archive.read_bytes()) == r['archive']['sha256']
with zipfile.ZipFile(archive) as z:
    infos = z.infolist()
    assert len(infos) == len(r['archive']['entries']) == 52
    assert len({i.filename for i in infos}) == len(infos)
    assert sum(i.file_size for i in infos) < 2 * 1024 * 1024
    assert z.testzip() is None
    data = {}
    for i in infos:
        p = PurePosixPath(i.filename)
        assert not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename
        assert not i.is_dir() and ((i.external_attr >> 16) & 0o170000) != 0o120000
        b = z.read(i)
        row = r['archive']['entries'][i.filename]
        assert len(b) == row['bytes'] and sha(b) == row['sha256']
        data[i.filename] = b
read = lambda n: json.loads(data[n].decode('utf-8-sig'))
manifest = read('native/deploy-manifest.json')
assert len(manifest['files']) == 14
gate.qualified_dependencies(manifest)
gate.qualify_worker(manifest, read('native/inspection-worker-qualification.json'), r['sourceCommit'])
gate.qualify_media_worker(manifest, read('native/media-worker-qualification.json'), r['sourceCommit'])
gate.qualify_copy_worker(manifest, read('native/copy-worker-qualification.json'), r['sourceCommit'])
s = read('native/stretch-worker-qualification.json')
gate.qualify_stretch_worker(manifest, s, r['sourceCommit'], r['sourceTree'])
assert s['checks'] == r['processReceiptChecks'] == 175
assert s['verifierChecks'] == r['artifactVerifierChecks'] == 1045
log = data['commands/continue-qualification.log'].decode()
line = next(l for l in log.splitlines() if l.startswith('{"checks":'))
counts = json.loads(line)
assert counts['checks'] == r['workerChecks'] == 173
assert counts['actualOwnedCompletedJobs'] == r['completedJobs'] == 34
assert counts['OSMemoryRefusal'] and counts['memoryValidSpanPositiveControl']
assert counts['nativePlatform'] == 'win32' and counts['nativeAudio'] is False
desktop = data['native/native-desktop-LastTest.log'].decode()
assert desktop.count('Test Passed.') == 2
assert 'stretch_ui_checks=48' in desktop and r['desktopUiChecks'] == 48
assert 'stretch_controller_checks=48' in desktop and r['desktopControllerChecks'] == 48
main = read('native/main-qualification.json')
window = read('native/main-window.json')
assert main['sourceCommit'] == r['sourceCommit'] and main['exitCode'] == r['mainExitCode'] == 0
assert main['sessionId'] == window['sessionId'] == r['mainInteractiveSession'] == 1
assert main['pid'] == window['pid'] and window['windowHandle'] > 0
assert main['title'] == window['title'] == 'SoundCurrent DAW'
assert next(i for i in manifest['files'] if i['path'] == 'soundcurrent-daw.exe')['sha256'] == main['exeSha256'] == window['exeSha256']
build = read('native/build-inputs.json')
assert build['sourceCommit'] == r['sourceCommit'] and build['sourceTree'] == r['sourceTree']
assert len(build['files']) == len({i['name'] for i in build['files']}) == r['nativeSourceInputs'] == 661
if args.check_current_inputs:
    for i in build['files']:
        assert sha((ROOT / gate.relative(i['name'])).read_bytes()) == i['sha256'], i['name']
raw = data['native/audio.wav']
assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE'
assert struct.unpack_from('<I', raw, 4)[0] == len(raw) - 8
assert raw[12:16] == b'fmt ' and raw[36:40] == b'data'
assert struct.unpack_from('<HHIIHH', raw, 20) == (3, 2, 48000, 384000, 8, 32)
assert struct.unpack_from('<I', raw, 40)[0] == len(raw) - 44 == 8192 * 8
samples = struct.unpack('<' + str(8192 * 2) + 'f', raw[44:])
assert all(math.isfinite(x) for x in samples) and max(abs(x) for x in samples) > 1
copy = read('native/copy-qualified.stdout')
c = read('native/copy-worker-qualification.json')
assert copy['workerPid'] == c['pid'] == c['reportedPid']
assert copy['phase'] == c['phase'] == 2 and copy['committed'] and not copy['sessionAssetPublished']
provenance = json.loads(copy['receipt'])
assert provenance['sourceSha256'] == provenance['stagedSha256'] == sha(raw)
assert provenance['frames'] == 8192 and provenance['channels'] == 2
inspection = read('native/sc-import-inspect-worker.exe.stdout')
assert inspection['source']['sha256'] == sha(data['native/owned.rpp'])
package = read('package/receipt.json')
audit = read('commands/package-audit.json')
assert package['sourceHead'] == audit['sourceHead'] == r['sourceCommit']
assert package['artifacts'] == audit['artifactIdentities'] == r['packaging']['artifactIdentities']
assert len(package['payload']) == audit['payloadFiles'] == 64
assert audit['sourceArchiveInputsChecked'] == 662 and audit['sourceArchiveInputsVerified']
assert not package['cleanInstallQualified'] and not package['nativeAudioReplayed'] and not package['releaseUploaded']
assert 'KeyError' in data['commands/qualify-native.log'].decode()
print('Retained Windows observations verified: 52 members, native receipt/manifest bindings, 173/34/1045 and 48/48 counters, owned raw/provenance, main session and local package metadata; no native or installer replay.')
