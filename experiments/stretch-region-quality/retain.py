#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain an owned diagnostic run, its PCM and its earlier failed attempts.
No application binaries, private audio, VM images or credentials are copied.
"""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('cache', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
cache = args.cache.resolve()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
run = cache / 'run-2'
entries = {}
sha = lambda data: hashlib.sha256(data).hexdigest()


def add(name, path):
    assert name not in entries
    entries[name] = path.read_bytes()


for name in ('summary.json', 'analysis.json', 'results.json', 'processes.json'):
    add(name, run / name)
for path in sorted((run / 'sources').iterdir()):
    add('sources/' + path.name, path)
for name in ('probe.cpp', 'build.py', 'run.py', 'analyze.py', 'retain.py'):
    add('experiment/' + name, Path(__file__).with_name(name))
add('experiment/rt_audit.cpp', root / 'tests/rt_audit.cpp')
add('experiment/rt_audit.hpp', root / 'tests/rt_audit.hpp')
for name in ('build.json', 'build.log', 'run-1.log', 'run-2.log'):
    add('logs/' + name, cache / name)
for folder in ('initial-build', 'pre-export-fix'):
    for name in ('build.json', 'build.log'):
        add('failures/' + folder + '/' + name, cache / folder / name)
for name in ('failure.json', 'processes.json'):
    add('failures/run-1/' + name, cache / 'run-1' / name)
add('failures/retention-initial.json', cache / 'retention-initial/failure.json')

processes = json.loads(entries['processes.json'])
rows = json.loads(entries['results.json'])
roots = {p['request']['operation']: Path(p['root']) for p in processes if p['kind'] == 'worker'}
assert len(roots) == 63
for operation, folder in roots.items():
    job = folder / 'media/derived' / operation
    for name in ('audio.wav', 'intent.json', 'complete.json', 'start.request'):
        add('renders/' + operation + '/' + name, job / name)
    add('renders/' + operation + '/project.json', folder / 'project.json')
for row in rows:
    add(f"crops/{row['id']:02d}-context.wav", roots[row['request']['operation']] / row['adoption']['relative'])
    add(f"crops/{row['id']:02d}-whole.wav", roots[row['wholeRequest']['operation']] / row['reference']['relative'])
tone_operations = {r['operation'] for r in json.loads(entries['analysis.json'])['steadyToneFrequencyObservations']}
assert len(tone_operations) == 6
for operation in tone_operations:
    add('tones/' + operation + '.wav', roots[operation] / 'exports/visible.wav')
for p in json.loads(entries['failures/run-1/processes.json']):
    if p['kind'] == 'worker':
        job = Path(p['root']) / 'media/derived' / p['request']['operation']
        for name in ('audio.wav', 'intent.json', 'complete.json', 'start.request'):
            add('failures/run-1/job/' + name, job / name)

inputs = json.loads(entries['logs/build.json'])['productionInputHashes']
inputs['tools/stretch_render_worker.cpp'] = sha((root / 'tools/stretch_render_worker.cpp').read_bytes())
entries['source-inputs.json'] = (json.dumps({'format': 'sc-region-quality-inputs-v1', 'inputs': inputs}, indent=2) + '\n').encode()
assert sum(map(len, entries.values())) < 48 * 1024 * 1024
with zipfile.ZipFile(output / 'capture.zip', 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for name, data in sorted(entries.items()):
        info = zipfile.ZipInfo(name, date_time=(2026, 10, 10, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o100644 << 16
        archive.writestr(info, data)
data = (output / 'capture.zip').read_bytes()
assert len(data) < 16 * 1024 * 1024
manifest = {'format': 'sc-region-quality-capture-v1', 'bytes': len(data), 'sha256': sha(data),
            'entries': {n: {'bytes': len(d), 'sha256': sha(d)} for n, d in sorted(entries.items())},
            'all63FullRenderedWavesRetained': True, 'all37CropPairsRetained': True,
            'allSixWholeToneWavesRetained': True, 'allFiveOriginalSourcesRetained': True,
            'nativeWindowsReplayed': False, 'nativeAudio': False, 'fullProcessingQuality': False,
            'productBinaryUploaded': False, 'currentPackageReceipt': False}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps({'entries': len(entries), 'zipBytes': len(data), 'uncompressedBytes': sum(map(len, entries.values()))}))
