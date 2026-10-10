#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained installed restart captures; never run archived code or audio."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import math
import struct
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--receipt', type=Path, default=Path(__file__).resolve().parents[1] /
                    'tests/results/M2/2026-10-10-installed-render-restart/qualification.json')
args = parser.parse_args()
receipt = json.loads(args.receipt.read_bytes())
sha = lambda b: hashlib.sha256(b).hexdigest()
archive = args.receipt.parent / 'capture.zip'
assert receipt['archive']['name'] == archive.name
assert archive.stat().st_size == receipt['archive']['bytes']
assert sha(archive.read_bytes()) == receipt['archive']['sha256']
z = zipfile.ZipFile(archive)
infos = z.infolist()
assert len(infos) == len({i.filename for i in infos}) == 160
assert sum(i.file_size for i in infos) < 32 * 1024 * 1024
assert set(z.namelist()) == set(receipt['archive']['entries']) and z.testzip() is None
for i in infos:
    path = PurePosixPath(i.filename)
    assert not path.is_absolute() and '..' not in path.parts and '\\' not in i.filename
    assert i.file_size < 4 * 1024 * 1024 and not i.is_dir()
    b = z.read(i)
    assert receipt['archive']['entries'][i.filename] == {'bytes': len(b), 'sha256': sha(b)}

def load(name):
    return json.loads(z.read(name))

def text(name):
    return z.read(name).decode('utf-8')

def wave(name):
    b = z.read(name)
    assert b[:4] in (b'RIFF', b'RF64') and b[8:12] == b'WAVE'
    offset, fmt, data, d64 = 12, None, None, None
    while offset + 8 <= len(b):
        tag, size = struct.unpack_from('<4sI', b, offset)
        offset += 8
        if tag == b'ds64':
            d64 = struct.unpack_from('<QQQ', b, offset)
        if tag == b'data' and size == 0xffffffff:
            assert d64
            size = d64[1]
        assert offset + size <= len(b)
        chunk = b[offset:offset + size]
        if tag == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', chunk)
            assert fmt[0] == 3 or (fmt[0] == 65534 and struct.unpack_from('<I', chunk, 24)[0] == 3)
        if tag == b'data':
            assert data is None
            data = chunk
        offset += size + (size & 1)
    assert offset == len(b) and fmt and data is not None
    assert fmt[5] == 32 and fmt[4] == fmt[1] * 4 and fmt[3] == fmt[2] * fmt[4]
    samples = struct.unpack('<' + 'f' * (len(data) // 4), data)
    assert all(math.isfinite(s) for s in samples)
    return {'channels': fmt[1], 'rate': fmt[2], 'frames': len(data) // fmt[4],
            'peak': max(abs(s) for s in samples), 'sha256': sha(b), 'sampleSha256': sha(data)}, data

w = 'workspace/'
p = w + 'project-été-Κиїв/'
initial = load(w + 'initial-project.json')
applied = load(w + 'applied-project.json')
assert load(w + 'undone-project.json') == initial
assert load(w + 'redone-project.json') == applied
assert load(w + 'before-reopen-project.json') == applied
assert load(p + 'project.json') == initial  # Explicit restoration after refusal fixtures.
stale = load(w + 'stale-project.json')
stale['tracks'][0]['clips'][0]['startFrame'] -= 1
assert stale == initial
original = initial['tracks'][0]['clips'][0]
clip = applied['tracks'][0]['clips'][0]
anchor = clip['stretch']
assert clip['id'] == original['id'] and clip['startFrame'] == original['startFrame'] == 0
assert clip['lengthFrames'] == 49152 and original['lengthFrames'] == 32768
settings = {'timeNumerator': 3, 'timeDenominator': 2, 'pitchMilliCents': 0, 'formantPreserved': True}
assert anchor['settings'] == settings and anchor['context'] is None
assert anchor['sourceAssetId'] == original['assetId']
assert anchor['sourceOrigin'] == {'frame': 0, 'fraction': 0, 'denominator': 1,
                                  'algorithm': 'soundcurrent.src-positioned-best-v1'}
assert anchor['sourceFrames'] == 32768 and anchor['warp']['markers'][0]['source'] == [4096, 0, 1]
assert anchor['warp']['markers'][0]['output'] == [6144, 0, 1]
assert len(anchor['warp']['markers']) == 1
operation = clip['assetId']
job = p + 'media/derived/' + operation + '/'
manifest_bytes = z.read(w + 'original-manifest.json')
assert z.read(p + 'media/stretch-selections/' + operation + '.json') == manifest_bytes
assert text(job + 'selection.sha256') == sha(manifest_bytes)
manifest = json.loads(manifest_bytes)
assert manifest['operation'] == operation and manifest['renderKey'] == anchor['renderKey']
request = manifest['request']
for key, value in settings.items():
    assert request[key] == value
assert request['warp'] == anchor['warp'] and request['contextEnabled'] is False
selected = json.loads(manifest['selection'])
assert selected['projectId'] == initial['projectId'] and selected['sampleRate'] == initial['sampleRate']
assert selected['tracks'][0]['id'] == initial['tracks'][0]['id']
assert selected['tracks'][0]['clips'] == [original] and selected['assets'] == initial['assets']
altered = load(w + 'altered-manifest.json')
assert altered != manifest
assert altered['request']['timeNumerator'] == 499999 and altered['request']['timeDenominator'] == 333333
altered['request']['timeNumerator'] = 3
altered['request']['timeDenominator'] = 2
assert altered == manifest
assert sha(z.read(w + 'altered-manifest.json')) != text(job + 'selection.sha256')
raw, rawdata = wave(p + 'media/raw-été.wav')
rendered, pcm = wave(job + 'audio.wav')
exported, epcm = wave(w + 'project-été-Κиїв-mix.wav')
repeated, rpcm = wave(w + 'reopened-mix.wav')
assert raw['sha256'] == initial['assets'][0]['sha256'] == anchor['sourceSha256']
assert raw['frames'] == 32768 and raw['peak'] == 1.5
assert rendered['frames'] == exported['frames'] == repeated['frames'] == 49152
assert rendered['channels'] == raw['channels'] == 2 and rendered['rate'] == raw['rate'] == 48000
assert rendered['peak'] == exported['peak'] == 1.5
assert pcm == epcm == rpcm and exported == repeated
assert z.read(w + 'project-été-Κиїв-mix.wav') == z.read(w + 'reopened-mix.wav')
# The unity protected span covers both channels, before any blending halo.
assert rawdata[3840 * 8:6144 * 8] == pcm[5888 * 8:8192 * 8]
complete, intent = load(job + 'complete.json'), load(job + 'intent.json')
assert complete['complete'] is True and intent['complete'] is False
assert complete['audioSha256'] == rendered['sha256'] and complete['sampleSha256'] == rendered['sampleSha256']
assert complete['target'] == complete['writtenFrames'] == 49152
for doc in [complete, intent]:
    assert doc['operation'] == operation and doc['sourceSha256'] == raw['sha256']
    assert doc['warp'] == anchor['warp'] and doc['protocol'] == 'sc-stretch-render-v5'
    # v5 carries the rounded target/warp, not the original rational multiplier.
    # Its exact numerator/denominator are bound separately by the parent manifest.
    for key in ['pitchMilliCents', 'formantPreserved']:
        assert doc[key] == settings[key]
assert complete['renderKey'] == anchor['renderKey']
assert applied['assets'][0] == initial['assets'][0] and len(applied['assets']) == 2
assert applied['assets'][1]['sha256'] == rendered['sha256']
pids = [text(w + f'app-pid-{i}.txt').strip() for i in range(1, 6)]
assert len(set(pids)) == 5 and all(p.isdecimal() for p in pids)
assert receipt['normalQuitExits'] == [0] * 5
for i in range(1, 6):
    assert text(w + f'exit-{i}.txt').strip() == '0'
for name in ['user-process-status.txt', 'helper-status.txt'] + [f'app-status-{i}.txt' for i in range(1, 6)]:
    status = text(w + name)
    assert 'CapEff:\t0000000000000000' in status and 'NoNewPrivs:\t1' in status
    assert 'Uid:\t1000\t1000\t1000\t1000' in status
assert not text(w + 'developer-tools.txt').strip()
assert text(w + 'helper-executable.txt').strip() == '/usr/bin/sc-stretch-render-worker'
assert '--memory-mib\n256' in text(w + 'helper-command.txt')
expected_nodes = sorted(['sc-preview-source', 'sc-preview-sink', 'Dummy-Driver', 'Freewheel-Driver'])
for name in ['graph-before', 'graph-audition-stopped'] + [f'graph-after-{i}' for i in range(1, 6)]:
    nodes = [x['info']['props']['node.name'] for x in load(w + name + '.json') if x['type'] == 'PipeWire:Interface:Node']
    assert sorted(nodes) == expected_nodes
assert load('container-exit.json')['exit'] == 0
command = load('container-command.json')['command']
assert '--private-network' in command and '--volatile=overlay' in command
assert '--setuid=1000' not in command  # Installer alone runs as root; app uses setpriv.
for action in ['open-stretch', 'set-markers', 'render-markers', 'quit-unattached',
               'open-recovery', 'scan-ready', 'review-ready', 'marker-guard',
               'audition-stop-apply', 'open-export', 'export-and-quit',
               'scan-attached', 'repeat-export-quit', 'scan-stale', 'scan-invalid']:
    record = load(action + '-command.json')
    assert record['exit_code'] == 0 and record['script'].endswith('/' + action + '.sh')
package = load('package-receipt.json')
assert package['source_head'] == receipt['sourceCommit'] == '5300010ab521917027501c831b550092e535c210'
assert package['source_tree'] == receipt['sourceTree'] == '83ff01f11fa64c32c424af8d5ed36091dfdbd37c'
assert text(w + 'installed-version.txt').strip() == package['version'] == receipt['version']
assert text(w + 'prior-version.txt').strip() == package['previous_version'] == receipt['priorVersion']
assert next(v['sha256'] for n, v in package['artifacts'].items() if n.endswith('.deb')) == receipt['packageSha256']
assert next(v['sha256'] for n, v in package['artifacts'].items() if n.endswith('source.tar.gz')) == receipt['sourceArchiveSha256']
installed = dict((line.split()[1], line.split()[0]) for line in text(w + 'installed-binaries.sha256').splitlines())
assert installed['/usr/bin/soundcurrent-daw'] == package['packaged_executable_sha256']
for path, digest in installed.items():
    if path != '/usr/bin/soundcurrent-daw':
        assert package['install_input_sha256'][path.lstrip('/')] == digest
native = load('native-linux-qualification.json')
assert native['source_head'] == receipt['sourceCommit'] and native['source_tree'] == receipt['sourceTree']
assert native['exit_code'] == 0 and native['selectedGroups'] == 7
assert native['input_hashes_unchanged_after_test'] is True
assert sha(z.read('native-qualification.log')) == native['log_sha256']
source = load('source-archive-verification.json')
assert source['sourceHead'] == receipt['sourceCommit'] and source['sourceTree'] == receipt['sourceTree']
assert source['everyTrackedBlobAndModeVerified'] is True and source['trackedMembersVerified'] == 2223
assert source['archiveSha256'] == receipt['sourceArchiveSha256']
digest = json.loads((args.receipt.parent / 'source-archive-digest.json').read_bytes())
assert digest['sourceHead'] == receipt['sourceCommit'] and digest['independentlyReadSha256'] == receipt['sourceArchiveSha256']
assert digest['matchesPreparedReceipt'] is True
observations = load(w + 'ui-observations.json')
assert observations['method'].startswith('Manual visual inspection')
assert len(observations['screenshots']) == 8
for name, observation in observations['screenshots'].items():
    assert observation['sha256'] == sha(z.read(w + name)) and observation['observed']
for key in ['installedHelperObserved', 'exactPcmEquality', 'protectedSpanPcmEquality',
            'repeatedExportByteEquality', 'undoRedoSemanticEquality', 'rawUnchanged',
            'readyAttachedStaleInvalidVisuallyObserved']:
    assert receipt[key] is True
for key in ['physicalAudio', 'nativePlayback', 'vmStarted', 'hostPackageInstalled',
            'abruptParentTerminationQualified', 'powerLossQualified', 'windowsInstalledRestartQualified',
            'fullQualityQualified', 'fullParity', 'installerUploaded']:
    assert receipt[key] is False
z.close()
print('installed_restart_evidence: 160 retained entries; five normal quits; source/package/helper bindings; '
      'raw/PCM/protected span/headroom; Undo/Redo; repeated export; bounded-scope audit passed. '
      'UI observations remain manual; no installed/native replay.')
