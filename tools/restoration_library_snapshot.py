#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Snapshot explicitly selected dependency candidate bytes; no loader or package authority."""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import stat

from restoration_library_bindings import bind_libraries, selected_bytes, MAX_FILES
from restoration_worker_images import linked, read, require


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode('utf-8')


def copy_selected(row, destination):
    """Bounded copy, matching the explicit source pin before and after copying."""
    selected_bytes(row)
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    size = 0; digest = hashlib.sha256()
    with os.fdopen(os.open(row['path'], flags), 'rb') as source:
        require(stat.S_ISREG(os.fstat(source.fileno()).st_mode), 'Selected source is no longer plain')
        with destination.open('xb') as target:
            while True:
                block = source.read(min(65536, row['bytes'] - size + 1))
                if not block: break
                size += len(block); require(size <= row['bytes'], 'Selected source grew during copy')
                require(target.write(block) == len(block), 'Short selected-library write'); digest.update(block)
            target.flush(); os.fsync(target.fileno())
    require(size == row['bytes'] and digest.hexdigest() == row['sha256'], 'Selected source copy differs from pin')
    selected_bytes(row)
    selected_bytes(dict(row, path=str(destination.absolute())))


def snapshot_libraries(directory, candidate, selections, output, *, selections_sha256, **context):
    """Publish a checked byte snapshot only after the report/source pins still agree."""
    binding = bind_libraries(directory, candidate, selections, selections_sha256=selections_sha256, **context)
    selection_raw = read(selections, 1024 * 1024)
    require(hashlib.sha256(selection_raw).hexdigest() == selections_sha256, 'Selection changed before snapshot')
    output = output.absolute()
    require('..' not in output.parts and not any(output.is_relative_to(p.resolve()) for p in (directory, candidate)),
            'Snapshot output overlaps a checked report or candidate directory')
    for parent in output.parents: require(not linked(parent), 'Linked snapshot parent')
    require(not output.exists() and not linked(output), 'Snapshot output must be new')
    output.mkdir()
    marker = encoded(dict(format='sc-restoration-library-snapshot-pending-v1', sourceCommit=binding['sourceCommit']))
    with (output/'snapshot.pending.json').open('xb') as stream: stream.write(marker)
    with (output/'selections.json').open('xb') as stream: stream.write(selection_raw)
    binding_raw = encoded(binding)
    with (output/'bindings.json').open('xb') as stream: stream.write(binding_raw)
    files = []
    for index, row in enumerate(binding['selectedFiles']):
        name = f'file-{index:03d}.bin'; copy_selected(row, output/name)
        files.append(dict(lookupName=row['lookupName'], path=name, bytes=row['bytes'], sha256=row['sha256']))
    require(bind_libraries(directory, candidate, selections, selections_sha256=selections_sha256, **context) == binding,
            'Source bindings changed during snapshot')
    require(read(output/'selections.json', 1024 * 1024) == selection_raw
            and read(output/'bindings.json', 2 * 1024 * 1024) == binding_raw
            and read(output/'snapshot.pending.json', 4096) == marker, 'Snapshot provenance changed')
    for row in files: selected_bytes(dict(row, path=str(output/row['path'])))
    expected = {'snapshot.pending.json', 'selections.json', 'bindings.json'} | {row['path'] for row in files}
    entries = list(itertools.islice(output.iterdir(), MAX_FILES + 8))
    require({p.name for p in entries} == expected and all(p.is_file() and not linked(p) for p in entries),
            'Unexpected or linked snapshot entry')
    receipt = dict(format='sc-restoration-library-snapshot-v1',
                   **{key: binding[key] for key in ['sourceCommit', 'nativePlatform', 'collectionSha256',
                        'candidateSha256', 'reportsSha256', 'selectionsSha256']},
                   bindingsSha256=hashlib.sha256(binding_raw).hexdigest(), selectedFiles=files,
                   unboundNames=binding['unboundNames'], snapshotBytesVerified=True,
                   **{key: value for key, value in binding.items() if key.endswith('Qualified') or key == 'packageAdmissionGranted'
                       or key == 'nativeExecutionPerformed'})
    raw = encoded(receipt)
    with (output/'receipt.pending.json').open('xb') as stream:
        stream.write(raw); stream.flush(); os.fsync(stream.fileno())
    require(read(output/'receipt.pending.json', 2 * 1024 * 1024) == raw, 'Snapshot receipt write differs')
    (output/'snapshot.pending.json').unlink()
    (output/'receipt.pending.json').replace(output/'snapshot.json')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reports', 'candidate', 'selections', 'output'): parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform', choices=['linux', 'win32'], required=True)
    for name in ('expected-source', 'collection-sha256', 'candidate-sha256', 'reports-sha256', 'selections-sha256'):
        parser.add_argument('--' + name, required=True)
    args = vars(parser.parse_args()); directory = args.pop('reports'); candidate = args.pop('candidate')
    selections = args.pop('selections'); output = args.pop('output')
    print(json.dumps(snapshot_libraries(directory, candidate, selections, output, **args)))


if __name__ == '__main__': main()
