#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind explicitly selected file bytes to direct names; never resolve or load them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat

from restoration_direct_dependencies import inventory_dependencies, FLAGS
from restoration_worker_images import decode_json, linked, read, require

MAX_FILE = 64 * 1024 * 1024
MAX_TOTAL = 128 * 1024 * 1024
MAX_FILES = 64


def selected_bytes(row):
    """Read one plain, explicitly named local file with bounded handle checks."""
    require(type(row) is dict and set(row) == {'lookupName', 'path', 'bytes', 'sha256'},
            'Invalid selected library fields')
    require(type(row['path']) is str and 0 < len(row['path']) <= 4096
            and not any(ord(c) < 32 for c in row['path']), 'Invalid selected library path')
    path = Path(row['path'])
    require(path.is_absolute() and '..' not in path.parts, 'Selected library path must be absolute')
    require(type(row['bytes']) is int and 0 < row['bytes'] <= MAX_FILE
            and type(row['sha256']) is str and re.fullmatch('[0-9a-f]{64}', row['sha256']),
            'Invalid selected library byte pin')
    for part in (path, *path.parents):
        require(not linked(part), 'Linked selected library path')
    before = path.stat()
    require(stat.S_ISREG(before.st_mode) and before.st_size == row['bytes'], 'Selected library extent differs')
    digest = hashlib.sha256(); size = 0
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NONBLOCK', 0) | getattr(os, 'O_NOFOLLOW', 0)
    with os.fdopen(os.open(path, flags), 'rb') as stream:
        opened = os.fstat(stream.fileno())
        require(stat.S_ISREG(opened.st_mode) and (opened.st_dev, opened.st_ino, opened.st_size) ==
                (before.st_dev, before.st_ino, before.st_size), 'Selected library changed before open')
        while True:
            block = stream.read(min(65536, row['bytes'] - size + 1))
            if not block: break
            size += len(block); require(size <= row['bytes'], 'Selected library grew'); digest.update(block)
        after = os.fstat(stream.fileno())
    # Some Windows Python versions expose creation time through path stat's
    # ctime and change time through fstat's ctime. Compare each API with itself.
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    for part in (path, *path.parents):
        require(not linked(part), 'Linked selected library path after read')
    final = path.stat()
    require(identity(before) == identity(opened) == identity(after) == identity(final)
            and before.st_ctime_ns == final.st_ctime_ns and opened.st_ctime_ns == after.st_ctime_ns
            and not linked(path) and size == row['bytes'] and digest.hexdigest() == row['sha256'],
            'Selected library bytes changed or differ from pin')
    return dict(row)


def bind_libraries(directory, candidate, selections, *, selections_sha256, **context):
    """Selected bytes are candidates only; name/ABI/loader/source rights stay unqualified."""
    require(type(selections_sha256) is str and re.fullmatch('[0-9a-f]{64}', selections_sha256),
            'Invalid library selection receipt pin')
    original = read(selections, 1024 * 1024)
    require(hashlib.sha256(original).hexdigest() == selections_sha256, 'Library selections differ from pin')
    plan = decode_json(original)
    keys = {'format', 'sourceCommit', 'nativePlatform', 'collectionSha256', 'candidateSha256', 'reportsSha256', 'files'}
    require(type(plan) is dict and set(plan) == keys and plan['format'] == 'sc-restoration-library-selections-v1',
            'Invalid library selection receipt')
    inventory = inventory_dependencies(directory, candidate, **context)
    for key in keys - {'format', 'files'}:
        require(type(plan[key]) is str and plan[key] == inventory[key], 'Library selection context differs')
    known = {d['lookupName'] for r in inventory['roles'] for d in r['dependencies']}
    require(type(plan['files']) is list and len(plan['files']) <= MAX_FILES, 'Excess selected libraries')
    seen = set(); total = 0; files = []
    for row in plan['files']:
        require(type(row) is dict and type(row.get('lookupName')) is str
                and row['lookupName'] in known and row['lookupName'] not in seen,
                'Unknown or repeated selected library name')
        require(type(row.get('bytes')) is int and row['bytes'] > 0, 'Invalid selected library extent')
        total += row['bytes']; require(total <= MAX_TOTAL, 'Excess selected library bytes')
        files.append(selected_bytes(row)); seen.add(row['lookupName'])
    require(inventory_dependencies(directory, candidate, **context) == inventory,
            'Direct inventory changed during file binding')
    require(read(selections, 1024 * 1024) == original, 'Library selections changed during file binding')
    for row in files:
        require(selected_bytes(row) == row, 'Selected library changed during final readback')
    return dict(format='sc-restoration-library-bindings-v1',
                **{key: inventory[key] for key in keys - {'format', 'files'}},
                selectionsSha256=selections_sha256, selectedFiles=files,
                unboundNames=sorted(known - seen), directRoles=inventory['roles'],
                selectedBytesVerified=True, loaderResolutionQualified=False,
                dependencyAbiQualified=False, dependencyLicensingQualified=False,
                **{key: False for key in FLAGS})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reports', 'candidate', 'selections'): parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform', choices=['linux', 'win32'], required=True)
    for name in ('expected-source', 'collection-sha256', 'candidate-sha256', 'reports-sha256', 'selections-sha256'):
        parser.add_argument('--' + name, required=True)
    args = vars(parser.parse_args()); directory = args.pop('reports'); candidate = args.pop('candidate')
    selections = args.pop('selections')
    print(json.dumps(bind_libraries(directory, candidate, selections, **args)))


if __name__ == '__main__': main()
