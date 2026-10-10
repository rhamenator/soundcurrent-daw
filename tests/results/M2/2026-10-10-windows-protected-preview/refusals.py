#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Negative retained-evidence tests; captured programs and scripts never execute."""
from pathlib import Path
from zipfile import ZipFile,ZIP_DEFLATED
from io import BytesIO
import json,hashlib,tempfile,subprocess,sys,shutil,struct
root=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
def packed(files):
    output=BytesIO()
    with ZipFile(output,'w',compression=ZIP_DEFLATED) as z:
        for n,data in files.items():z.writestr(n,data)
    return output.getvalue()
def change_json(files,name,update):
    value=json.loads(files[name].decode('utf-8-sig'));update(value);files[name]=(json.dumps(value)+'\n').encode()
cases={
    'scope':'Conservative scope',
    'member-bytes':'Exact captured member',
    'unexpected-path':'Fixed captured membership',
    'native-head':'Native source binding',
    'lost-terminal':'Actual bounded v5 terminal',
    'installer-source':'Matching package source/payload',
    'fresh-os':'Existing SDK-free independent clone scope',
    'abnormal-close':'Actual installed normal close',
    'sdk-dll':'Actual installed local DLL binding',
    'undo':'Saved actual Apply/Undo/Redo/reopen states',
    'marker':'Exact persisted protected controls',
    'export-samples':'Whole installed exported PCM values/headroom and repeated export',
    'native-request-marker':'Native request/completion geometry binding',
    'native-profile':'Exact claimed native profile geometry',
    'duration-type':'Native requested duration/context controls',
    'installed-completion-marker':'Installed completion/persisted geometry binding',
    'installed-completion-pitch':'Installed completion/persisted geometry binding',

}
for case,expected in cases.items():
    with tempfile.TemporaryDirectory(prefix='sc-win-retained-refusal-') as td:
        target=Path(td);shutil.copyfile(root/'verify.py',target/'verify.py')
        manifest=json.loads((root/'manifest.json').read_text())
        with ZipFile(root/'capture.zip') as z:files={n:z.read(n) for n in z.namelist()}
        if case=='scope':manifest['fullQualityQualified']=True
        elif case=='member-bytes':files['native/producer.py']+=b'altered'
        elif case=='unexpected-path':files['../unowned']=b'No execution'
        elif case=='native-head':change_json(files,'native/source-before.json',lambda d:d.update(sourceCommit='0'*40))
        elif case=='lost-terminal':change_json(files,'native/uniform/terminal.json',lambda d:d.update(exitCode=None,retirementPending=True))
        elif case=='installer-source':change_json(files,'package/receipt.json',lambda d:d.update(sourceHead='0'*40))
        elif case in ('native-request-marker','native-profile','duration-type'):
            request='native/nonuniform/request.json'
            if case=='duration-type':change_json(files,request,lambda d:d['request'].update(timeDenominator=2.0))
            else:
                def update(d):d['request']['warp']['markers'][1]['output'][0]=18432
                change_json(files,request,update)
                if case=='native-profile':
                    def update(d):d['warp']['markers'][1]['output'][0]=18432
                    change_json(files,'native/nonuniform/complete.json',update)

        else:
            name='installed/cceed-history.zip' if case=='undo' else 'installed/diagnostic2.zip' if case=='fresh-os' else 'installed/closed-20261010125851896.zip'
            with ZipFile(BytesIO(files[name])) as z:inner={n:z.read(n) for n in z.namelist()}
            def named(suffix):return next(n for n in inner if n.replace('\\','/').endswith('/'+suffix))
            if case=='fresh-os':change_json(inner,named('setup-result.json'),lambda d:d.update(pristineOs=True))
            elif case=='abnormal-close':change_json(inner,named('main-20261010125851896.json'),lambda d:d.update(exitCode=1))
            elif case=='sdk-dll':
                def update(d):
                    next(m for m in d['modules'] if m['name']=='Qt6Core.dll')['path']='C:\\developer-sdk\\Qt6Core.dll'
                change_json(inner,named('main-20261010125851896.json'),update)
            elif case=='undo':
                def update(d):d['tracks'][0]['clips'][0]['lengthFrames']=49152
                change_json(inner,named('project-undo.json'),update)
            elif case=='marker':
                # Make all accepted states agree on the wrong marker, so the exact
                # marker invariant (rather than only snapshot equality) refuses it.
                for outer in ['installed/cceed-history.zip','installed/closed-20261010125851896.zip']:
                    with ZipFile(BytesIO(files[outer])) as z:content={n:z.read(n) for n in z.namelist()}
                    for n in content:
                        if n.replace('\\','/').endswith(('/project-applied.json','/project-redo.json','/project-été-Κиїв/project.json')):
                            def update(d):
                                st=d['tracks'][0]['clips'][0]['stretch']
                                if st:st['warp']['markers'][0]['output'][0]=8192
                            change_json(content,n,update)
                    files[outer]=packed(content)
                inner=None
            elif case in ('installed-completion-marker','installed-completion-pitch'):
                for outer in ['installed/closed-20261010124948390.zip','installed/closed-20261010125851896.zip']:
                    with ZipFile(BytesIO(files[outer])) as z:content={n:z.read(n) for n in z.namelist()}
                    n=next(n for n in content if n.replace('\\','/').endswith('/complete.json'))
                    def update(d):
                        if case=='installed-completion-marker':d['warp']['markers'][0]['output'][0]=8192
                        else:d['pitchMilliCents']=500
                    change_json(content,n,update);files[outer]=packed(content)
                inner=None
            elif case=='export-samples':
                n=named('project-été-Κиїв-mix.wav');raw=bytearray(inner[n]);at=12
                while at+8<=len(raw):
                    tag=raw[at:at+4];size=struct.unpack_from('<I',raw,at+4)[0];at+=8
                    if tag==b'data':struct.pack_into('<f',raw,at,.001);break
                    at+=size+(size&1)
                else:raise AssertionError('Owned export has no PCM')
                inner[n]=bytes(raw)
            if inner is not None:files[name]=packed(inner)
        raw=packed(files);(target/'capture.zip').write_bytes(raw)
        manifest.update(bytes=len(raw),sha256=sha(raw))
        if case!='member-bytes':manifest['entries']={n:{'bytes':len(d),'sha256':sha(d)} for n,d in files.items()}
        (target/'manifest.json').write_text(json.dumps(manifest)+'\n')
        p=subprocess.run([sys.executable,str(target/'verify.py')],capture_output=True,text=True,timeout=30)
        assert p.returncode!=0 and expected in p.stderr,(case,p.returncode,p.stderr)
        print(case+': refused for '+expected)
print('windows_protected_preview_refusals: 17 meaningful negative cases; no native replay')
