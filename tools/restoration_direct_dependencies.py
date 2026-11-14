#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract direct dependency names from checked reports; never resolve libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from restoration_dependency_reports import verify_reports, MAX_LOG
from restoration_worker_images import decode_json, read, require

MAX_DEPENDENCIES = 256
FLAGS = ['nativeExecutionPerformed', 'dependencyClosureQualified', 'dependencySourceBundleQualified',
         'standaloneBundleQualified', 'packageAdmissionGranted', 'installerQualified', 'installedGuiQualified']


def dependency_names(raw, platform):
    """Parse the observed English inspector layouts, retaining order and repeats."""
    require(platform in ('linux', 'win32') and type(raw) is bytes and 0 < len(raw) <= MAX_LOG,
            'Invalid direct dependency report input')
    require(all(byte >= 32 or byte in (9, 10, 13) for byte in raw), 'Control byte in dependency report')
    lines = raw.splitlines()
    require(len(lines) <= 16384, 'Excess dependency report lines')
    names = []
    def append(token, kind):
        require(len(names) < MAX_DEPENDENCIES
                and re.fullmatch(rb'[A-Za-z0-9_+\-][A-Za-z0-9_.+\-]{0,254}', token),
                'Unsafe or excessive dependency name')
        name = token.decode('ascii')
        if platform == 'win32': require(name.lower().endswith('.dll'), 'Non-DLL import name')
        names.append(dict(name=name, lookupName=name if platform == 'linux' else name.lower(), kind=kind))
    if platform == 'linux':
        nonempty = [line for line in lines if line.strip()]
        require(len(nonempty) >= 3, 'Incomplete dynamic dependency report')
        header = re.fullmatch(rb'Dynamic section at offset 0x[0-9a-fA-F]+ contains ([0-9]{1,4}) entries:', nonempty[0].strip())
        require(header is not None, 'Unrecognized dynamic dependency report')
        count = int(header[1])
        require(0 < count <= 4096 and len(nonempty) == count + 2
                and re.fullmatch(rb'\s*Tag\s+Type\s+Name/Value\s*', nonempty[1]), 'Incomplete dynamic table')
        tags = []
        for line in nonempty[2:]:
            entry = re.fullmatch(rb'\s*0x[0-9a-fA-F]+\s+\(([A-Z0-9_]+)\)\s+(.*)', line)
            require(entry is not None, 'Malformed dynamic table row')
            tag, value = entry[1], entry[2]; tags.append(tag)
            if tag != b'NEEDED': continue
            match = re.fullmatch(rb'Shared library:\s+\[([^\]]+)\]\s*', value)
            require(match is not None, 'Malformed NEEDED entry')
            append(match[1], 'needed')
        require(tags[-1] == b'NULL' and b'NULL' not in tags[:-1], 'Incomplete dynamic table terminator')

    else:
        state = 'before'; saw_type = False
        for line in lines:
            text = line.strip()
            if text == b'File Type: EXECUTABLE IMAGE':
                require(not saw_type and state == 'before', 'Repeated or misplaced image type'); saw_type = True
            elif text == b'Image has the following dependencies:':
                require(state == 'before' and saw_type, 'Repeated or misplaced import table'); state = 'imports'
            elif text.startswith(b'Image has '):
                require(False, 'Unsupported dependency report section')
            elif text == b'Summary':
                require(state == 'imports', 'Missing or repeated import table'); state = 'summary'
            elif state == 'imports' and text:
                require(line.startswith(b'    '), 'Malformed import row'); append(text, 'import')
            elif state != 'imports' and text.lower().endswith(b'.dll'):
                require(False, 'Import name outside declared table')
        require(saw_type and state == 'summary' and names, 'Incomplete dependency import report')
    return names


def inventory_dependencies(directory, candidate, *, expected_source, platform, collection_sha256,
                           candidate_sha256, reports_sha256):
    """Read pinned direct names only; no search, library load, copy, or subprocess."""
    context = dict(expected_source=expected_source, platform=platform, collection_sha256=collection_sha256,
                   candidate_sha256=candidate_sha256, reports_sha256=reports_sha256)
    verified = verify_reports(directory, candidate, **context)
    raw = read(directory / 'reports.json', 65536)
    require(hashlib.sha256(raw).hexdigest() == reports_sha256, 'Dependency receipt changed')
    manifest = decode_json(raw)
    bindings = {row['path']: row for row in manifest['fileBindings']}
    roles = []
    for row in verified['reports']:
        raw = read(directory / row['stdout'], MAX_LOG); binding = bindings[row['stdout']]
        require(len(raw) == binding['bytes'] and hashlib.sha256(raw).hexdigest() == binding['sha256'],
                'Dependency stdout differs from pin')
        roles.append(dict(role=row['role'], imagePath=row['imagePath'], imageBytes=row['imageBytes'],
                          imageSha256=row['imageSha256'], stdoutSha256=binding['sha256'],
                          dependencies=dependency_names(raw, platform)))
    require(json.dumps(verify_reports(directory, candidate, **context), sort_keys=True)
            == json.dumps(verified, sort_keys=True), 'Reports changed during name extraction')
    return dict(format='sc-restoration-direct-dependencies-v1', sourceCommit=expected_source,
                nativePlatform=platform, collectionSha256=collection_sha256, candidateSha256=candidate_sha256,
                reportsSha256=reports_sha256, reportsVerified=True, directNamesExtracted=True, roles=roles,
                **{name: False for name in FLAGS})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--platform', choices=['linux', 'win32'], required=True)
    parser.add_argument('--expected-source', required=True)
    parser.add_argument('--collection-sha256', required=True)
    parser.add_argument('--candidate-sha256', required=True)
    parser.add_argument('--reports-sha256', required=True)
    args = parser.parse_args()
    result = inventory_dependencies(args.reports, args.candidate, expected_source=args.expected_source,
            platform=args.platform, collection_sha256=args.collection_sha256,
            candidate_sha256=args.candidate_sha256, reports_sha256=args.reports_sha256)
    print(json.dumps(result))


if __name__ == '__main__': main()
