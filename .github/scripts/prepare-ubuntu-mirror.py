#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Hosted Ubuntu CI only: replace the measured slow Azure archive URI."""
from pathlib import Path
import argparse, hashlib, json, os, re, stat

AZURE = re.compile(r'(?<!\S)https?://azure\.archive\.ubuntu\.com/ubuntu(/?)(?!\S)')
PRIMARY = 'https://archive.ubuntu.com/ubuntu'


def rewrite(text, deb822):
    """Change URI fields only; preserve suites, keys, options and comments."""
    output = []
    uri_continuation = False
    for line in text.splitlines(keepends=True):
        content, separator, comment = line.partition('#')
        if deb822:
            if not content.strip():
                if not separator:
                    uri_continuation = False
                output.append(line)
                continue
            if content.startswith('URIs:'):
                uri_continuation = True
                prefix, body = 'URIs:', content[5:]
            elif content[:1].isspace() and uri_continuation:
                prefix, body = '', content
            else:
                if content.strip():
                    uri_continuation = False
                # A blank paragraph terminates a field; a comment does not.
                elif not separator:
                    uri_continuation = False
                output.append(line)
                continue
        else:
            match = re.match(r'^(\s*deb(?:-src)?\s+(?:\[[^\]\r\n]*\]\s+)?)(\S+)(.*)$', content, re.S)
            if not match:
                output.append(line)
                continue
            prefix, uri, rest = match.groups()
            output.append(prefix + AZURE.sub(lambda m: PRIMARY + m[1], uri) + rest + separator + comment)
            continue
        output.append(prefix + AZURE.sub(lambda m: PRIMARY + m[1], body) + separator + comment)
    return ''.join(output)


def prepare(root):
    """Trusted runner namespace; validate every candidate before any write."""
    if root.is_symlink() or not root.is_dir():
        raise ValueError('APT root must be a plain directory')
    candidates = []
    legacy = root / 'sources.list'
    if legacy.exists() or legacy.is_symlink():
        candidates.append(legacy)
    directory = root / 'sources.list.d'
    if directory.exists() or directory.is_symlink():
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError('APT source directory must be plain')
        candidates.extend(sorted(p for p in directory.iterdir() if p.suffix in ('.list', '.sources')))
    changes, rows = [], []
    for path in candidates:
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError('APT source must be a plain unshared file')
        original = path.read_bytes()
        updated = rewrite(original.decode('utf-8'), path.suffix == '.sources').encode('utf-8')
        rows.append(dict(path=str(path.relative_to(root)), changed=updated != original,
                         beforeSha256=hashlib.sha256(original).hexdigest(),
                         afterSha256=hashlib.sha256(updated).hexdigest()))
        if original != updated:
            changes.append((path, updated))
    for path, updated in changes:
        path.write_bytes(updated)  # Preserve the existing file's owner and mode.
    return dict(format='sc-ci-ubuntu-mirror-v1', changedFiles=len(changes), files=rows,
                packageAuthentication='unchanged', primary=PRIMARY)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    if any(os.environ.get(key) != value for key, value in
           [('GITHUB_ACTIONS', 'true'), ('RUNNER_OS', 'Linux'), ('RUNNER_ENVIRONMENT', 'github-hosted')]):
        raise ValueError('Mirror preparation is restricted to GitHub-hosted Linux CI')
    release = dict(line.split('=', 1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    if release.get('ID', '').strip('"') != 'ubuntu' or release.get('VERSION_ID', '').strip('"') != '24.04':
        raise ValueError('Mirror preparation requires the reviewed Ubuntu 24.04 runner')
    result = prepare(Path('/etc/apt'))
    args.evidence.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
