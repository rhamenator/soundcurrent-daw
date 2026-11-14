#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify pinned snapshot bytes without accessing original selected source paths."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re

from restoration_direct_dependencies import inventory_dependencies, FLAGS
from restoration_library_bindings import MAX_FILES, MAX_FILE, MAX_TOTAL, selected_bytes
from restoration_worker_images import decode_json, linked, read, require

CONTEXT = ['sourceCommit', 'nativePlatform', 'collectionSha256', 'candidateSha256', 'reportsSha256']
DENIED = FLAGS + ['loaderResolutionQualified', 'dependencyAbiQualified', 'dependencyLicensingQualified']


def same(left, right):
    require(json.dumps(left, sort_keys=True, allow_nan=False) == json.dumps(right, sort_keys=True, allow_nan=False),
            'Snapshot receipt schema, context or value differs')


def entries(root, expected):
    for path in (root, *root.parents): require(not linked(path), 'Linked snapshot path')
    require(root.is_dir(), 'Missing snapshot directory')
    paths = list(itertools.islice(root.iterdir(), MAX_FILES + 8))
    require({p.name for p in paths} == expected and all(p.is_file() and not linked(p) for p in paths),
            'Missing, pending, extra or linked snapshot entry')


def verify_snapshot(root, directory, candidate, *, snapshot_sha256, selections_sha256, **context):
    for pin in (snapshot_sha256, selections_sha256):
        require(type(pin) is str and re.fullmatch('[0-9a-f]{64}', pin), 'Invalid explicit snapshot pin')
    root = root.absolute()
    require('..' not in root.parts, 'Ambiguous snapshot path')
    for path in (root, *root.parents): require(not linked(path), 'Linked snapshot path')
    inventory = inventory_dependencies(directory, candidate, **context)
    raw = read(root/'snapshot.json', 2 * 1024 * 1024)
    require(hashlib.sha256(raw).hexdigest() == snapshot_sha256, 'Snapshot receipt differs from pin')
    selection_raw = read(root/'selections.json', 1024 * 1024)
    require(hashlib.sha256(selection_raw).hexdigest() == selections_sha256, 'Selection receipt differs from pin')
    binding_raw = read(root/'bindings.json', 2 * 1024 * 1024)
    selection = decode_json(selection_raw); binding = decode_json(binding_raw); receipt = decode_json(raw)
    require(type(selection) is dict and set(selection) == {'format', 'files', *CONTEXT}
            and type(selection['files']) is list and len(selection['files']) <= MAX_FILES,
            'Invalid or excessive snapshot selections')
    known = {d['lookupName'] for role in inventory['roles'] for d in role['dependencies']}
    files = []; seen = set(); total = 0
    for index, row in enumerate(selection['files']):
        require(type(row) is dict and set(row) == {'lookupName', 'path', 'bytes', 'sha256'},
                'Invalid retained selection fields')
        require(type(row['lookupName']) is str and row['lookupName'] in known and row['lookupName'] not in seen,
                'Unknown or repeated retained lookup name')
        require(type(row['path']) is str and 0 < len(row['path']) <= 4096
                and not any(ord(c) < 32 for c in row['path'])
                and any(p.is_absolute() and '..' not in p.parts for p in (PurePosixPath(row['path']), PureWindowsPath(row['path']))),
                'Invalid original-path provenance')
        # Original paths are opaque provenance; never open, resolve or stat them.
        require(type(row['bytes']) is int and 0 < row['bytes'] <= MAX_FILE
                and type(row['sha256']) is str and re.fullmatch('[0-9a-f]{64}', row['sha256']),
                'Invalid retained selected-byte pin')
        total += row['bytes']; require(total <= MAX_TOTAL, 'Excess snapshot byte total')
        files.append(dict(row, path=f'file-{index:03d}.bin')); seen.add(row['lookupName'])
    expected = {'snapshot.json', 'selections.json', 'bindings.json'} | {row['path'] for row in files}
    entries(root, expected)
    same(selection, dict(format='sc-restoration-library-selections-v1', **{k: inventory[k] for k in CONTEXT},
                         files=selection['files']))
    unbound = sorted(known - seen)
    same(binding, dict(format='sc-restoration-library-bindings-v1', **{k: inventory[k] for k in CONTEXT},
                       selectionsSha256=selections_sha256, selectedFiles=selection['files'], unboundNames=unbound,
                       directRoles=inventory['roles'], selectedBytesVerified=True, **{k: False for k in DENIED}))
    binding_sha = hashlib.sha256(binding_raw).hexdigest()
    same(receipt, dict(format='sc-restoration-library-snapshot-v1', **{k: inventory[k] for k in CONTEXT},
                       selectionsSha256=selections_sha256, bindingsSha256=binding_sha, selectedFiles=files,
                       unboundNames=unbound, snapshotBytesVerified=True, **{k: False for k in DENIED}))
    for row in files: selected_bytes(dict(row, path=str(root/row['path'])))
    require(inventory_dependencies(directory, candidate, **context) == inventory, 'Direct reports changed during snapshot verification')
    require(read(root/'snapshot.json', 2 * 1024 * 1024) == raw
            and read(root/'selections.json', 1024 * 1024) == selection_raw
            and read(root/'bindings.json', 2 * 1024 * 1024) == binding_raw, 'Snapshot provenance changed during verification')
    for row in files: selected_bytes(dict(row, path=str(root/row['path'])))
    entries(root, expected)
    return dict(format='sc-restoration-library-snapshot-verification-v1', **{k: inventory[k] for k in CONTEXT},
                snapshotSha256=snapshot_sha256, selectionsSha256=selections_sha256, bindingsSha256=binding_sha,
                snapshotVerified=True, originalSourcePathsAccessed=False, selectedFiles=files, unboundNames=unbound,
                **{k: False for k in DENIED})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('snapshot', 'reports', 'candidate'): parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform', choices=['linux', 'win32'], required=True)
    for name in ('expected-source', 'collection-sha256', 'candidate-sha256', 'reports-sha256', 'selections-sha256', 'snapshot-sha256'):
        parser.add_argument('--' + name, required=True)
    args = vars(parser.parse_args()); root = args.pop('snapshot'); directory = args.pop('reports'); candidate = args.pop('candidate')
    print(json.dumps(verify_snapshot(root, directory, candidate, **args)))


if __name__ == '__main__': main()
