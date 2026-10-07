#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare an owned Ubuntu 26.04 amd64 DEB preview from a qualified clean source.

No system installation, release upload, audio connection or daemon configuration.
Needs maintainer packaging tools; recipients only need the declared runtime packages.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()
def command(args, cwd=ROOT, env=None):
    result = subprocess.run(list(map(str,args)), cwd=cwd, env=env,
                            capture_output=True, text=True, check=True)
    return result.stdout.strip()
def require(value, message):
    if not value:
        raise RuntimeError(message)
def verify_build_source(cache, root):
    entries = [line.split('=',1)[1] for line in cache.splitlines()
               if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    require(len(entries)==1 and Path(entries[0]).resolve()==root.resolve(),
            'Build tree belongs to another source checkout')
def verify_staged_install(stage, root, executable):
    expected = {
        'usr/bin/soundcurrent-daw': executable,
        'usr/share/licenses/soundcurrent-daw/equipment-GPL-3.0.txt': root/'reuse/equipment/upstream/data/equipment/LICENSE',
        'usr/share/doc/soundcurrent-daw/equipment-provenance.json': root/'reuse/equipment/provenance.json',
        'usr/share/applications/soundcurrent-daw.desktop': root/'packaging/soundcurrent-daw.desktop',
        'usr/share/icons/hicolor/scalable/apps/soundcurrent-daw.svg': root/'packaging/soundcurrent-daw.svg',
    }
    found = {}
    for path in stage.rglob('*'):
        require(not path.is_symlink(), 'Unexpected install payload symlink: '+str(path))
        if path.is_file(): found[str(path.relative_to(stage))]=path
    require(set(found)==set(expected), 'Install payload file set differs from current source')
    for relative, source in expected.items():
        require(digest(found[relative])==digest(source),
                'Install payload differs from qualified source: '+relative)
    return {relative:digest(source) for relative,source in expected.items()}
def package(args):
    require(not command(['git','status','--porcelain']), 'Commit the tested source before packaging')
    host = dict(line.split('=',1) for line in Path('/etc/os-release').read_text().splitlines() if '=' in line)
    require(host.get('ID','').strip('"')=='ubuntu' and host.get('VERSION_ID','').strip('"')=='26.04',
            'This first preview builder is qualified only for Ubuntu 26.04')
    arch = command(['dpkg','--print-architecture'])
    require(arch=='amd64','First preview qualification is amd64 only')
    build = args.build_dir.resolve()
    executable = build/'soundcurrent-daw'
    qualification = json.loads(args.qualification.read_text())
    require(qualification.get('exit_code')==0, 'Qualification did not pass')
    require(qualification.get('executable_sha256',{}).get('soundcurrent-daw')==digest(executable),
            'Desktop executable differs from the tested executable')
    for relative, sha in qualification['source_sha256'].items():
        require(digest(ROOT/relative)==sha, 'Tested input changed: '+relative)
    cache = (build/'CMakeCache.txt').read_text()
    verify_build_source(cache,ROOT)
    require('SC_BUILD_PIPEWIRE:BOOL=ON' in cache,'A recording preview requires native PipeWire')
    build_type = next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_BUILD_TYPE:'))
    head = command(['git','rev-parse','HEAD'])
    maintainer = command(['git','show','-s','--format=%an <%ae>',head])
    date = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d')
    version = '0.1.0~preview.'+date+'.'+head[:12]
    output = args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
    stage = output/'stage'; stage.mkdir()
    receipt = {'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'source_head':head,'source_tree':command(['git','rev-parse','HEAD^{tree}']),
               'platform':'Ubuntu 26.04 amd64','build_type':build_type,'version':version,
               'qualified_executable_sha256':digest(executable),
               'qualification_sha256':digest(args.qualification),
               'native_audio_run':False,'system_installation':False,'clean_install_qualified':False,
               'release_uploaded':False}
    log=[]
    def run(argv,cwd=ROOT,env=None):
        try:
            process=subprocess.run(list(map(str,argv)),cwd=cwd,env=env,
                                   capture_output=True,text=True,check=True)
        except subprocess.CalledProcessError as error:
            log.append({'argv':list(map(str,argv)),'exit':error.returncode,
                        'stdout':error.stdout,'stderr':error.stderr})
            raise
        log.append({'argv':list(map(str,argv)),'exit':0,'stdout':process.stdout,'stderr':process.stderr})
        return process.stdout.strip()
    try:
        environment=dict(os.environ,DESTDIR=str(stage))
        run(['cmake','--install',build,'--prefix','/usr'],env=environment)
        receipt['install_input_sha256']=verify_staged_install(stage,ROOT,executable)
        receipt['every_CMake_install_input_matches_current_source']=True
        documentation=stage/'usr/share/doc/soundcurrent-daw';documentation.mkdir(parents=True,exist_ok=True)
        licenses=stage/'usr/share/licenses/soundcurrent-daw';licenses.mkdir(parents=True,exist_ok=True)
        for relative in ['README.md','CHANGELOG.md','THIRD-PARTY-NOTICES.md','docs/88-easy-installation.md','docs/89-workflow-previews.md']:
            shutil.copy2(ROOT/relative,documentation/Path(relative).name)
        shutil.copy2(ROOT/'LICENSE',licenses/'GPL-3.0.txt')
        (documentation/'source.txt').write_text(
            f'GPL-3.0-only. Exact application source: {head}\n'
            f'https://github.com/rhamenator/soundcurrent-daw/tree/{head}\n'
            'The matching source archive is supplied alongside this local preview.\n'
            'System runtime libraries are supplied by Ubuntu packages with their own licenses/source.\n')
        # Work on the copied executable; retain the exact qualified original.
        run(['strip','--strip-unneeded',stage/'usr/bin/soundcurrent-daw'])
        (output/'debian').mkdir()
        (output/'debian/control').write_text(f'Source: soundcurrent-daw\nSection: sound\nPriority: optional\nMaintainer: {maintainer}\nStandards-Version: 4.7.0\n\nPackage: soundcurrent-daw\nArchitecture: amd64\nDescription: SoundCurrent DAW workflow preview\n')
        shlibs=run(['dpkg-shlibdeps','-O','-e'+str(stage/'usr/bin/soundcurrent-daw')],cwd=output)
        depends=next(line[len('shlibs:Depends='):] for line in shlibs.splitlines() if line.startswith('shlibs:Depends='))
        # Runtime-loaded Qt plugins and the existing user daemon are not in ELF NEEDED.
        depends+=', qt6-qpa-plugins (>= 6.10.2), pipewire (>= 1.6.2), libpipewire-0.3-0t64 (>= 1.6.2)'
        control=stage/'DEBIAN';control.mkdir()
        (control/'control').write_text(f'Package: soundcurrent-daw\nVersion: {version}\nArchitecture: {arch}\nMaintainer: {maintainer}\nSection: sound\nPriority: optional\nDepends: {depends}\nHomepage: https://github.com/rhamenator/soundcurrent-daw\nDescription: SoundCurrent DAW recording workflow preview\n Early GPL recording, in-process EQ, project and WAV-export preview.\n Tested Ubuntu 26.04 amd64 only; full DAW parity and sustained hardware\n recording qualification remain unfinished.\n')
        for p in stage.rglob('*'):
            if p.is_file():p.chmod(0o755 if p==stage/'usr/bin/soundcurrent-daw' else 0o644)
            elif p.is_dir():p.chmod(0o755)
        run(['desktop-file-validate',stage/'usr/share/applications/soundcurrent-daw.desktop'])
        deb=output/f'soundcurrent-daw_{version}_{arch}.deb'
        run(['dpkg-deb','--root-owner-group','--build',stage,deb])
        source=output/f'soundcurrent-daw-{version}-source.tar.gz'
        run(['git','archive','--format=tar.gz','--prefix=soundcurrent-daw/','-o',source,head])
        extracted=output/'extracted';run(['dpkg-deb','--extract',deb,extracted])
        installed_binary=extracted/'usr/bin/soundcurrent-daw'
        require(digest(installed_binary)==digest(stage/'usr/bin/soundcurrent-daw'),'Extracted executable changed')
        for p in stage.rglob('*'):
            if p.is_file() and 'DEBIAN' not in p.relative_to(stage).parts:
                require(digest(p)==digest(extracted/p.relative_to(stage)),'Extracted payload changed')
        probe_env=dict(os.environ,QT_QPA_PLATFORM='offscreen',XDG_CONFIG_HOME=str(output/'probe-preferences'))
        run([installed_binary,'--version'],env=probe_env)
        run([installed_binary,'--help'],env=probe_env)
        receipt.update({'status':'prepared-local-preview','depends':depends,
                        'artifacts':{p.name:{'bytes':p.stat().st_size,'sha256':digest(p)} for p in [deb,source]},
                        'packaged_executable_sha256':digest(installed_binary),
                        'all_extracted_payload_bytes_match':True,'offscreen_startup_help_version':True})
        print(json.dumps({'package':str(deb),'source':str(source),'clean_install_qualified':False}))
    except Exception as error:
        receipt.update({'status':'failed-preserved','error':str(error)})
        raise
    finally:
        (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        (output/'commands.json').write_text(json.dumps(log,indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--qualification',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    package(parser.parse_args())
