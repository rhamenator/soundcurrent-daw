#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare a local, unsigned Windows x64 installer and matching source bundle.

Consumes retained native build/deployment inputs; never activates audio, installs
software, changes a VM, or uploads a release. Clean-install acceptance is separate.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
QT_SOURCE = 'a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307'
SNDFILE_SOURCE = 'ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121'
REDIST = 'cc0ff0eb1dc3f5188ae6300faef32bf5beeba4bdd6e8e445a9184072096b713b'

def require(value, message):
    if not value:
        raise ValueError(message)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def relative(value):
    p = PurePosixPath(value)
    require(re.fullmatch(r'[A-Za-z0-9_./-]+',value) and not p.is_absolute() and
            p.parts and '..' not in p.parts and p.as_posix()==value and len(value)<=200,
            'Unsafe payload path')
    require(all(not part.endswith('.') and
                part.split('.')[0].upper() not in {'CON','PRN','AUX','NUL',
                    *('COM'+str(n) for n in range(1,10)),
                    *('LPT'+str(n) for n in range(1,10))} for part in p.parts),
            'Unsafe Windows payload component')
    return p

def deploy_payload(archive, manifest, destination):
    expected = {}
    for row in manifest['files']:
        name = relative(row['path']).as_posix()
        require(name.lower() not in {n.lower() for n in expected}, 'Duplicate manifest path')
        expected[name] = row
    required = {'soundcurrent-daw.exe','sndfile.dll','Qt6Core.dll','Qt6Gui.dll',
                'Qt6Widgets.dll','platforms/qwindows.dll'}
    require(required <= set(expected) and len(expected)<=64, 'Missing/excess deployment payload')
    require(all(0<r['bytes']<=64*1024*1024 for r in expected.values()) and
            sum(r['bytes'] for r in expected.values())<=256*1024*1024,
            'Deployment bounds exceeded')
    require(not any(Path(n).name.lower().startswith(('vcruntime','msvcp','ucrtbase'))
                    for n in expected), 'Use official runtime installer, not developer CRT DLLs')
    require(archive.stat().st_size<=300*1024*1024, 'Deployment archive bounds exceeded')
    with zipfile.ZipFile(archive) as z:
        require(len(z.infolist())<=128, 'Deployment entry bounds exceeded')
        # Compress-Archive uses Windows separators. Normalize before comparing
        # membership, retaining duplicate checks and refusing drive/traversal paths.
        files = [(relative(m.filename.replace('\\','/')).as_posix(),m)
                 for m in z.infolist() if not m.is_dir()]
        require(len(files)==len(expected) and {n for n,m in files}==set(expected),
                'Deployment membership mismatch')
        for m in z.infolist():
            relative(m.filename.replace('\\','/').rstrip('/'))
            require((m.external_attr>>16)&0o170000 != 0o120000, 'Deployment symlink refused')
            if not m.is_dir():
                row=expected[relative(m.filename.replace('\\','/')).as_posix()]
                require(m.file_size==row['bytes'], 'Deployment size mismatch')
        require(z.testzip() is None, 'Deployment CRC failure')
        for name,m in files:
            row = expected[name]
            require(m.file_size==row['bytes'] and 0<m.file_size<=64*1024*1024,
                    'Deployment size mismatch')
            data = z.read(m)
            require(hashlib.sha256(data).hexdigest()==row['sha256'], 'Deployment hash mismatch')
            p = destination.joinpath(*relative(name).parts)
            p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    return expected

def qualified_dependencies(manifest):
    trusted = read_json(ROOT/'research/windows-preview-dependencies.json')
    require(trusted['qtSourceSha256']==QT_SOURCE and
            trusted['sndfileSourceSha256']==SNDFILE_SOURCE, 'Dependency source anchor differs')
    dependencies = {r['path']:r for r in manifest['files'] if r['path']!='soundcurrent-daw.exe'}
    require(dependencies==trusted['files'], 'Unqualified dependency binary identity')

def nsis_path(path):
    text = str(path.resolve())
    require(not any(c in text for c in '$"\r\n'), 'Unsupported NSIS source path')
    return text

def scripts(stage, output, sequence, head, runtime):
    entries = sorted(p for p in stage.rglob('*') if p.is_file())
    put, remove, preflight = [], [], []
    for index,p in enumerate(entries):
        name = str(relative(p.relative_to(stage).as_posix())).replace('/','\\')
        parent = str(relative(p.relative_to(stage).as_posix()).parent).replace('/','\\')
        put += ['SetOutPath "$INSTDIR'+('\\'+parent if parent!='.' else '')+'"',
                'File "'+nsis_path(p)+'"']
        remove += ['IfFileExists "$INSTDIR\\'+name+'" 0 remove_done_'+str(index),
                   'ClearErrors', 'Delete "$INSTDIR\\'+name+'"',
                   'IfErrors removal_failed', 'remove_done_'+str(index)+':']
        if p.suffix.lower() in {'.exe','.dll'}:
            preflight += ['IfFileExists "$INSTDIR\\'+name+'" 0 check_done_'+str(index),
                "System::Call 'kernel32::CreateFileW(w \"$INSTDIR\\"+name+"\", i 0x40000000, i 7, p 0, i 3, i 0, p 0) p.r0'",
                '${If} $0 == -1',
                'MessageBox MB_OK "Close applications using this preview before continuing."',
                'SetErrorLevel 2','Abort','${EndIf}',
                "System::Call 'kernel32::CloseHandle(p r0)'",'check_done_'+str(index)+':'
            ]
    dirs = sorted({p.parent for p in entries if p.parent!=stage},key=lambda p:len(p.parts),reverse=True)
    remove += ['RMDir "$INSTDIR\\'+p.relative_to(stage).as_posix().replace('/','\\')+'"' for p in dirs]
    for name,lines in [('payload.nsh',put),('remove.nsh',remove),('preflight.nsh',preflight)]:
        (output/name).write_text('\n'.join(lines)+'\n')
    installer=output/f'SoundCurrent-DAW-{sequence}-{head[:12]}-x64-setup.exe'
    text=(ROOT/'packaging/windows/preview.nsi.in').read_text()
    values={'BUILD_ID':sequence+'-'+head[:12],'SEQUENCE':sequence,'INSTALLER':nsis_path(installer),
            'LICENSE':nsis_path(ROOT/'LICENSE'),'REDIST':nsis_path(runtime),
            'PAYLOAD_INCLUDE':nsis_path(output/'payload.nsh'),
            'REMOVE_INCLUDE':nsis_path(output/'remove.nsh'),
            'PREFLIGHT_INCLUDE':nsis_path(output/'preflight.nsh')}
    for key,value in values.items():text=text.replace('@'+key+'@',value)
    require(not re.search(r'@[A-Z_]+@',text), 'Unresolved installer template')
    script=output/'preview.nsi';script.write_text(text)
    return script,installer

def package(args):
    require(not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip(),
            'Commit source before producing installer/source artifacts')
    require(re.fullmatch(r'[0-9]{14}',args.sequence), 'Invalid frozen preview sequence')
    datetime.datetime.strptime(args.sequence,'%Y%m%d%H%M%S')
    head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    qualification=read_json(args.main_qualification)
    require(qualification['exitCode']==0 and qualification['sessionId']>0,
            'Actual native main launch/close not qualified')
    build=read_json(args.build_inputs)
    for row in build['files']:
        require(digest(ROOT.joinpath(*relative(row['name']).parts))==row['sha256'],
                'Native input changed: '+row['name'])
    for p,sha in [(args.qt_source,QT_SOURCE),(args.sndfile_source,SNDFILE_SOURCE),(args.runtime,REDIST)]:
        require(digest(p)==sha, 'Pinned external source/runtime mismatch: '+p.name)
    manifest=read_json(args.deploy_manifest)
    qualified_dependencies(manifest)
    row=next((r for r in manifest['files'] if r['path']=='soundcurrent-daw.exe'),{})
    require(row.get('sha256')==qualification['exeSha256'], 'Deployed main differs from qualified main')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=False)
    stage=output/'payload';stage.mkdir()
    deploy_payload(args.deploy_zip,manifest,stage)
    legal=stage/'licenses';legal.mkdir()
    for name,path in {'GPL-3.0.txt':ROOT/'LICENSE','libsndfile-LGPL.txt':ROOT/'third_party/libsndfile/COPYING',
                      'nlohmann-MIT.txt':ROOT/'third_party/nlohmann/LICENSE.MIT',
                      'equipment-GPL.txt':ROOT/'reuse/equipment/upstream/data/equipment/LICENSE',
                      'NSIS-copyright.txt':Path('/usr/share/doc/nsis/copyright')}.items():
        shutil.copy2(path,legal/name)
    with tarfile.open(args.qt_source) as t:
        for m in t.getmembers():
            if m.isfile() and m.name.startswith('qtbase-everywhere-src-6.12.0/LICENSES/'):
                (legal/('Qt-'+Path(m.name).name)).write_bytes(t.extractfile(m).read())
    with tarfile.open(args.sndfile_source) as t:
        for m in t.getmembers():
            if m.isfile() and m.name.endswith(('/src/ALAC/LICENSE','/src/GSM610/COPYRIGHT')):
                (legal/('libsndfile-'+m.name.split('/')[-2]+'.txt')).write_bytes(t.extractfile(m).read())
    shutil.copy2(ROOT/'THIRD-PARTY-NOTICES.md',stage/'THIRD-PARTY-NOTICES.md')
    shutil.copy2(args.qt_sdk/'sbom/qtbase-6.12.0.spdx.json',stage/'QtBase-SBOM.json')
    (stage/'SOURCE.txt').write_text(f'GPL-3.0-only. Exact SoundCurrent source: {head}\n'
        'Matching application/QtBase/libsndfile source archives are supplied beside setup.\n'
        'Shared Qt 6.12.0 and libsndfile 1.2.2 DLLs can be replaced/relinked.\n'
        'Qt deployment uses windeployqt without compiler runtimes. MSVC runtime is\n'
        'the independent signed Microsoft 14.44.35211.0 installer; no SDK is required.\n')
    script,installer=scripts(stage,output,args.sequence,head,args.runtime)
    r=subprocess.run(['makensis','-V3',script],capture_output=True,text=True)
    (output/'makensis.log').write_text(r.stdout+r.stderr)
    require(r.returncode==0,'Installer compiler failed; retained log')
    source=output/f'soundcurrent-daw-{args.sequence}-{head[:12]}-source.tar.gz'
    subprocess.run(['git','archive','--format=tar.gz','--prefix=soundcurrent-daw/','-o',str(source),head],cwd=ROOT,check=True)
    dependencies=output/'dependency-source';dependencies.mkdir()
    for p in [args.qt_source,args.sndfile_source]:shutil.copy2(p,dependencies/p.name)
    artifacts=[installer,source,*sorted(dependencies.iterdir())]
    receipt={'sourceHead':head,'sequence':args.sequence,'status':'prepared-local-unsigned-preview',
             'cleanInstallQualified':False,'nativeAudioReplayed':False,'releaseUploaded':False,
             'buildInputsReceiptSha256':digest(args.build_inputs),'mainQualificationSha256':digest(args.main_qualification),
             'deploymentManifestSha256':digest(args.deploy_manifest),'officialRuntimeSha256':REDIST,
             'installerSourceSha256':digest(ROOT/'packaging/windows/preview.nsi.in'),
             'payload':{p.relative_to(stage).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(stage.rglob('*')) if p.is_file()},
             'artifacts':{p.relative_to(output).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in artifacts},
             'upgradePolicy':'Separate owned preview directories; prior builds/projects/preferences preserved',
             'uninstallPolicy':'Exact generated owned-file list; no recursive project or preference deletion'}
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (output/'SHA256SUMS').write_text(''.join(digest(p)+'  '+p.relative_to(output).as_posix()+'\n' for p in artifacts))
    print(json.dumps({'installer':str(installer),'source':str(source),'cleanInstallQualified':False}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['deploy-zip','deploy-manifest','build-inputs','main-qualification','qt-source','sndfile-source','runtime','qt-sdk','output']:
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--sequence',required=True)
    package(parser.parse_args())
