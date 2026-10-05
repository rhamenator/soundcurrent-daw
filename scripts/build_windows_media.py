#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explicit Linux->Windows headless development build; no install or publish."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache/windows-sndfile'
URL = 'https://codeload.github.com/libsndfile/libsndfile/tar.gz/refs/tags/1.2.2'
SHA256 = 'ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121'
CACHE.mkdir(parents=True, exist_ok=True)
archive = CACHE / 'libsndfile-1.2.2.tar.gz'
if not archive.exists():
    with urllib.request.urlopen(URL, timeout=30) as stream:
        data = stream.read(16 * 1024 * 1024 + 1)
    if len(data) > 16 * 1024 * 1024 or hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError('libsndfile archive size/hash mismatch')
    # Only a verified archive enters the build cache.
    with archive.open('xb') as stream:
        stream.write(data)
if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
    raise RuntimeError('Cached libsndfile archive hash mismatch')
source = CACHE / 'libsndfile-1.2.2'
if not source.exists():
    with tarfile.open(archive) as stream:
        stream.extractall(CACHE, filter='data')
# Cached source must remain exactly upstream; never build a modified cache.
with tarfile.open(archive) as stream:
    for entry in stream.getmembers():
        if entry.isfile():
            if stream.extractfile(entry).read() != (CACHE / entry.name).read_bytes():
                raise RuntimeError('Modified cached libsndfile source: ' + entry.name)
build = CACHE / 'build'
def run(args):
    subprocess.run(args, cwd=ROOT, check=True)
flags = {
    'CMAKE_SYSTEM_NAME': 'Windows',
    'CMAKE_C_COMPILER': 'x86_64-w64-mingw32-gcc',
    'CMAKE_CXX_COMPILER': 'x86_64-w64-mingw32-g++',
    'CMAKE_BUILD_TYPE': 'Release',
    'BUILD_SHARED_LIBS': 'ON',
    'ENABLE_EXTERNAL_LIBS': 'OFF',
    'ENABLE_MPEG': 'OFF',
    'BUILD_PROGRAMS': 'OFF',
    'BUILD_EXAMPLES': 'OFF',
    'BUILD_TESTING': 'OFF',
    'BUILD_REGTEST': 'OFF',
    'ENABLE_EXPERIMENTAL': 'OFF',
    'INSTALL_PKGCONFIG_MODULE': 'OFF',
    'PYTHON_EXECUTABLE': '/usr/bin/python3',
    'CMAKE_POLICY_VERSION_MINIMUM': '3.5',
}
run(['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja'] +
    [f'-D{key}={value}' for key, value in flags.items()])
run(['cmake', '--build', str(build), '-j', '4'])
daw = ROOT / '.cache/build-windows-core'
run(['cmake', '-S', '.', '-B', str(daw),
     '-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-mingw.cmake',
     '-DSC_BUILD_MEDIA=ON', f'-DSNDFILE_LIBRARY={build}/libsndfile.dll.a'])
run(['cmake', '--build', str(daw), '-j', '4'])
shutil.copy2(build / 'libsndfile.dll', daw / 'libsndfile.dll')
(CACHE / 'source.json').write_text(json.dumps({
    'version': '1.2.2', 'source': URL, 'sha256': SHA256,
    'bytes': archive.stat().st_size, 'build_flags': flags,
}, indent=2) + '\n')
print('Headless Windows binaries built; native execution and runtime packaging remain unqualified.')
