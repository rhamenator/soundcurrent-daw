#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reject source/install provenance mismatches on owned independent inputs."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('preview_builder',ROOT/'tools/package_linux_preview.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
legacy='0.1.0~preview.20261007.d23d3632f09e'
first=builder.preview_version('20261008001000','f'*40,legacy)
second=builder.preview_version('20261008001001','0'*40,first)
assert subprocess.run(['dpkg','--compare-versions',second,'gt',first]).returncode==0
for sequence,head,previous in [('20261008001000','0'*40,second),
                               ('20261301001000','0'*40,None),
                               ('202610080010','0'*40,None),
                               ('20261008001000','unqualified',None)]:
    try:builder.preview_version(sequence,head,previous)
    except RuntimeError:pass
    else:raise AssertionError('Unordered or invalid preview version accepted')
owned=Path(tempfile.mkdtemp(prefix='sc-preview-package-'))
print(f'Owned preview package fixture root: "{owned}"',flush=True)
source=owned/'source';source.mkdir();other=owned/'other-checkout';other.mkdir()
builder.verify_build_source(f'CMAKE_HOME_DIRECTORY:INTERNAL={source}\n',source)
for cache in [f'CMAKE_HOME_DIRECTORY:INTERNAL={other}\n','',
              f'CMAKE_HOME_DIRECTORY:INTERNAL={source}\nCMAKE_HOME_DIRECTORY:INTERNAL={other}\n']:
    try:builder.verify_build_source(cache,source)
    except RuntimeError:pass
    else:raise AssertionError('Unverified build source directory accepted')
inputs={
    'usr/bin/soundcurrent-daw':'qualified-binary',
    'usr/share/licenses/soundcurrent-daw/equipment-GPL-3.0.txt':'reuse/equipment/upstream/data/equipment/LICENSE',
    'usr/share/doc/soundcurrent-daw/equipment-provenance.json':'reuse/equipment/provenance.json',
    'usr/share/applications/soundcurrent-daw.desktop':'packaging/soundcurrent-daw.desktop',
    'usr/share/icons/hicolor/scalable/apps/soundcurrent-daw.svg':'packaging/soundcurrent-daw.svg',
}
stage=owned/'stage'
for installed,relative in inputs.items():
    original=source/relative;original.parent.mkdir(parents=True,exist_ok=True)
    original.write_bytes(('Independent source fixture: '+relative+'\n').encode())
    payload=stage/installed;payload.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(original,payload)
binary=source/'qualified-binary'
assert len(builder.verify_staged_install(stage,source,binary))==5
def refuse(name):
    try:builder.verify_staged_install(stage,source,binary)
    except RuntimeError as error:(owned/(name+'.txt')).write_text(str(error)+'\n')
    else:raise AssertionError('Mismatched installed source accepted: '+name)
for installed,relative in inputs.items():
    payload=stage/installed;before=payload.read_bytes()
    payload.write_bytes(before+b'Stale build payload\n');refuse('stale-'+Path(relative).name)
    assert (source/relative).read_bytes()==before,'Verification wrote the original'
    payload.write_bytes(before)
extra=stage/'usr/share/applications/old-unqualified.desktop';extra.write_text('Stale extra payload')
refuse('extra-install-file');extra.unlink()
icon=stage/'usr/share/icons/hicolor/scalable/apps/soundcurrent-daw.svg';before=icon.read_bytes();icon.unlink()
refuse('missing-icon');icon.symlink_to(source/'packaging/soundcurrent-daw.svg')
refuse('symlink-payload');icon.unlink();icon.write_bytes(before)
assert len(builder.verify_staged_install(stage,source,binary))==5
print('PASS: build-source binding; five independent stale payloads, extra/missing/symlink refusal; exact payload retry; originals unchanged.')

# New desktop versions require the sibling worker from the exact qualified build.
(source/'ui').mkdir();(source/'ui/import_inspection_dialog.cpp').write_text('fixture marker')
worker=binary.parent/'sc-import-inspect-worker';worker.write_bytes(b'qualified worker')
refuse('missing-worker')
installed=stage/'usr/bin/sc-import-inspect-worker';installed.write_bytes(worker.read_bytes())
assert len(builder.verify_staged_install(stage,source,binary))==6
installed.write_bytes(b'stale worker');refuse('stale-worker')
installed.write_bytes(worker.read_bytes())
assert len(builder.verify_staged_install(stage,source,binary))==6
print('New desktop payload: qualified worker required; missing/stale worker refused.')
