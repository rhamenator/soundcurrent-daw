#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently inspect owned Windows records/PCM; never execute captures."""
from pathlib import Path, PurePosixPath
from zipfile import ZipFile
from io import BytesIO
import hashlib,json,math,struct
root=Path(__file__).resolve().parent
checks=0
def check(value,why):
    global checks
    checks+=1
    if not value: raise AssertionError(why)
def sha(raw): return hashlib.sha256(raw).hexdigest()
def decode(raw): return json.loads(raw.decode('utf-8-sig'))
COMMIT='cceedcc6e6d468a4a22cff617bf26d58515888e8'
TREE='5db68c1e903606eb5965f09175b5a983fc78cc64'
WORKER='514bee45bc78a8c7b0173753c92bd6934c64010c9191be867b060f86f850ce7d'
MAIN='f4e50623f7e5e204f9ce5695304bad77ef7f145333c4d7c2b266904435de1ed7'
EXPECTED=frozenset(['controls/capture.ps1', 'controls/history.ps1', 'controls/reopen.ps1', 'installed/cceed-history.zip', 'installed/closed-20261010124948390.zip', 'installed/closed-20261010125851896.zip', 'installed/diagnostic2.zip', 'installed/main-20261010124948390.zip', 'installed/main-20261010125851896.zip', 'native/bank-terminal.json', 'native/build-inputs.json', 'native/build-terminal.json', 'native/copy-worker-qualification.json', 'native/identity/audio.wav', 'native/identity/complete.json', 'native/identity/intent.json', 'native/identity/prototype-render.json', 'native/identity/prototype-rendered.wav', 'native/identity/prototype-source.wav', 'native/identity/request.json', 'native/identity/terminal.json', 'native/identity/worker.stderr', 'native/identity/worker.stdout', 'native/inspection-worker-qualification.json', 'native/main-qualification.json', 'native/main-terminal.json', 'native/media-worker-qualification.json', 'native/native-desktop-LastTest.log', 'native/nonuniform/audio.wav', 'native/nonuniform/complete.json', 'native/nonuniform/intent.json', 'native/nonuniform/prototype-render.json', 'native/nonuniform/prototype-rendered.wav', 'native/nonuniform/prototype-source.wav', 'native/nonuniform/request.json', 'native/nonuniform/terminal.json', 'native/nonuniform/worker.stderr', 'native/nonuniform/worker.stdout', 'native/observer.py', 'native/one-protected-complete.json', 'native/producer.py', 'native/protected-warp-qualification.json', 'native/source-before.json', 'native/stretch-worker-qualification.json', 'native/uniform/audio.wav', 'native/uniform/complete.json', 'native/uniform/intent.json', 'native/uniform/prototype-render.json', 'native/uniform/prototype-rendered.wav', 'native/uniform/prototype-source.wav', 'native/uniform/request.json', 'native/uniform/terminal.json', 'native/uniform/worker.stderr', 'native/uniform/worker.stdout', 'package/receipt.json', 'package/source-export-qualified.json'])

def safe(name):
    check('\\' not in name and ':' not in name and not name.startswith('/') and '..' not in PurePosixPath(name).parts,'Safe captured path')
    check(PurePosixPath(name).name not in ('private-transport.json','receiver-private.json') and PurePosixPath(name).suffix.lower() not in ('.exe','.dll','.msi'),'No secret or product executable capture')
def strict(a,b):
    # Python otherwise equates True/1 and 3/3.0, unlike this protocol contract.
    if type(a) is not type(b):return False
    if isinstance(a,dict):return a.keys()==b.keys() and all(strict(a[k],b[k]) for k in a)
    if isinstance(a,list):return len(a)==len(b) and all(strict(x,y) for x,y in zip(a,b))
    return a==b
def bind_request(q,c):
    fields=('protocol','processor','operation','assetId','relative','rate','channels','sourceFrames','first','firstFraction','firstDenominator','frames','pitchMilliCents','formantPreserved','warp')
    check(all(k in c and strict(q[k],c[k]) for k in fields) and strict(q['sha256'],c['sourceSha256']),'Native request/completion geometry binding')
    check(type(q['timeNumerator']) is int and type(q['timeDenominator']) is int and q['timeNumerator']>0 and q['timeDenominator']>0 and q['contextEnabled'] is False and q['contextBefore']==q['contextAfter']==0,'Native requested duration/context controls')
    target=(q['frames']*q['timeNumerator']+q['timeDenominator']-1)//q['timeDenominator']
    check(type(c['target']) is int and c['target']==c['writtenFrames']==target and c['sourceAlgorithm']=='soundcurrent.src-positioned-best-v1','Native exact duration/source timing binding')
def expected_warp(outputs):
    source_positions=[4096,12288,20480,28672]
    markers=[];spans=[];points=[{'source':[0,0,1],'output':[0,0,1]}]
    for i,(x,y) in enumerate(zip(source_positions,outputs)):
        owner=f'00000000-0000-0000-0000-{i+1:012x}'
        markers.append({'id':owner,'source':[x,0,1],'output':[y,0,1]})
        spans.append({'owner':owner,'sourceBegin':[x-256,0,1],'sourceAnchor':[x,0,1],'sourceEnd':[x+2048,0,1],'outputBegin':[y-256,0,1],'outputAnchor':[y,0,1],'outputEnd':[y+2048,0,1]})
        points.extend([{'source':[x-256,0,1],'output':[y-256,0,1]},{'source':[x,0,1],'output':[y,0,1]},{'source':[x+2048,0,1],'output':[y+2048,0,1]}])
    target=32768 if outputs==source_positions else 49152
    points.append({'source':[32768,0,1],'output':[target,0,1]})
    return {'mode':'transient-protected-v1','before':256,'after':2048,'halo':64,'minimumNonunityGap':64,'chunkFrames':512,'map':'soundcurrent.warp-piecewise-rational-v1','markers':markers,'spans':spans,'points':points}
manifest=decode((root/'manifest.json').read_bytes())
check(manifest['format']=='sc-windows-protected-preview-observation-v1','Observation format')
check(manifest['sourceCommit']==COMMIT and manifest['sourceTree']==TREE,'Exact retained source')
check(manifest['nativeV5Completed'] is True and manifest['installedProtectedWorkflowObserved'] is True,'Observed bounded progress')
for field in ('pristineOs','physicalAudio','fullQualityQualified','causeResolved','releaseUploaded'):
    check(manifest[field] is False,'Conservative scope '+field)
raw=(root/'capture.zip').read_bytes()
check(len(raw)==manifest['bytes'] and sha(raw)==manifest['sha256'] and len(raw)<3*1024*1024,'Exact capture archive')
with ZipFile(BytesIO(raw)) as z:
    check(set(z.namelist())==EXPECTED and len(z.namelist())==len(EXPECTED),'Fixed captured membership')
    check(sum(i.file_size for i in z.infolist())<8*1024*1024,'Bounded decompression')
    files={n:z.read(n) for n in z.namelist()}
check(set(manifest['entries'])==EXPECTED,'Fixed manifest membership')
for n,data in files.items():
    safe(n);binding=manifest['entries'][n]
    check(len(data)==binding['bytes'] and sha(data)==binding['sha256'],'Exact captured member '+n)
def read(n):return decode(files[n])
def bound(n):
    d=read(n);check(d['sourceCommit']==COMMIT and d['sourceTree']==TREE,'Native source binding '+n);return d
before=bound('native/source-before.json')
check(before['sourceClean'] and before['parallel']==2 and before['priority']=='BelowNormal' and not before['nativeAudio'],'Actual clean bounded build')
build=bound('native/build-terminal.json')
check(build['exitCode']==0 and build['exeSha256']['sc-stretch-render-worker']==WORKER and build['exeSha256']['soundcurrent-daw']==MAIN,'Actual native build and binary bindings')
inputs=bound('native/build-inputs.json')
check(len(inputs['files'])==873 and len({f['name'] for f in inputs['files']})==873 and all(len(f['sha256'])==64 for f in inputs['files']),'Actual selected build input coverage')
check(bound('native/bank-terminal.json')['exitCode']==0,'Actual native bank terminal')
v4=bound('native/stretch-worker-qualification.json')
check(v4['nativePlatform']=='win32' and v4['exeSha256']==WORKER and v4['exitCode']==v4['verifierExitCode']==0 and v4['checks']==385 and v4['verifierChecks']==1045 and v4['verifiedArtifact'] and not v4['nativeAudio'],'Native v4 receipt')
v5=bound('native/protected-warp-qualification.json')
check(v5['workerSha256']==WORKER and v5['workerProcesses']==33 and v5['completedRenders']==21 and v5['checks']==519 and not v5['nativeAudio'] and not v5['fullQualityQualified'],'Native v5 bank scope')
check(len(v5['workers'])==33 and len({w['request']['operation'] for w in v5['workers']})==33,'Distinct owned bank operations (PIDs may be reused)')
check(sum(w['exit']==0 for w in v5['workers'])==21 and sum(w['exit']==1 for w in v5['workers'])==12,'Actual complete/refusal outcomes')
for w in v5['workers']:
    check(w['pid']>0 and w['request']['protocol']=='sc-stretch-render-v5','Owned v5 process/request')
    if w['exit']==0:
        check(w['ready']['operation']==w['request']['operation'] and len(w['completion'])==1 and w['completion'][0]['complete'] and w['completion'][0]['operation']==w['request']['operation'],'Actual ready/completion identity')
        bind_request(w['request'],w['completion'][0])
    else:
        check(not w['completion'] and (w['ready'] is None or w['ready']['operation']==w['request']['operation']) and not decode(w['stderr'].encode())['publicationMayHaveCommitted'],'Expected prepublication refusal')
check(len(v5['comparisons'])==18 and all(c['allPcmEqual'] and c['prototypePcmSha256']==c['workerPcmSha256'] for c in v5['comparisons']),'Native reported whole PCM comparisons')
for n,exe in [('inspection-worker-qualification.json','sc-import-inspect-worker.exe'),('media-worker-qualification.json','sc-approved-wave-probe.exe'),('copy-worker-qualification.json','sc-media-import-worker.exe')]:
    d=read('native/'+n)
    check(d['sourceCommit']==COMMIT and d['nativePlatform']=='win32' and d['exitCode']==0 and d['pid']==d['reportedPid']>0 and d['exeSha256']==build['exeSha256'][exe.removesuffix('.exe')] and not d['nativeAudio'],'Independent helper binding '+n)
main=bound('native/main-qualification.json')
check(main['exeSha256']==MAIN and main['pid']>0 and main['sessionId']==1 and main['windowHandle']>0 and main['normalClose'] and main['exitCode']==0 and not main['forcedRetirement'] and not main['styleOverride'] and not main['nativeAudio'],'Native developer main startup/close')
log=files['native/native-desktop-LastTest.log'].decode('utf-8-sig')
check(log.count('Test Passed.')==5 and all(s in log for s in ('worker-observation-failures','stretch-ui','desktop-protected-warp-ui','desktop-stretch-controller','protected-warp-package-inputs')),'Native targeted CTest outcomes')
def pcm(raw):
    check(raw[:4] in (b'RIFF',b'RF64') and raw[8:12]==b'WAVE','Owned float WAV envelope')
    at=12;size64=None;data=None;fmt=None
    while at+8<=len(raw):
        tag=raw[at:at+4];size=struct.unpack_from('<I',raw,at+4)[0];at+=8
        if tag==b'data' and size==0xffffffff:
            check(size64 is not None,'RF64 data size');size=size64
        check(size<=len(raw)-at,'Bounded WAV chunk')
        if tag==b'ds64':
            check(size>=28,'RF64 size record');size64=struct.unpack_from('<Q',raw,at+8)[0]
        if tag==b'fmt ':
            check(size>=16,'Float format extent');fmt=struct.unpack_from('<HHIIHH',raw,at)
            if fmt[0]==0xfffe:
                check(size>=40 and raw[at+24:at+40]==bytes.fromhex('0300000000001000800000aa00389b71'),'Float extensible subtype')
            else:check(fmt[0]==3,'Float encoding')
        if tag==b'data':data=raw[at:at+size]
        at+=size+(size&1)
    check(fmt is not None and fmt[1:3]==(2,48000) and fmt[-1]==32 and data is not None,'Exact float layout')
    values=struct.unpack('<'+'f'*(len(data)//4),data)
    check(all(math.isfinite(value) for value in values),'Complete finite PCM')
    return data,values
profiles=[]
for profile in ('identity','uniform','nonuniform'):
    base='native/'+profile+'/'
    request=bound(base+'request.json');terminal=bound(base+'terminal.json');complete=read(base+'complete.json')
    q=request['request'];bind_request(q,complete)
    outputs={'identity':[4096,12288,20480,28672],'uniform':[6144,18432,30720,43008],'nonuniform':[6144,16384,32768,43008]}[profile]
    expected={'rate':48000,'channels':2,'sourceFrames':32768,'first':0,'firstFraction':0,'firstDenominator':1,'frames':32768,'timeNumerator':1 if profile=='identity' else 3,'timeDenominator':1 if profile=='identity' else 2,'pitchMilliCents':0,'formantPreserved':True,'contextEnabled':False,'contextBefore':0,'contextAfter':0}
    check(all(strict(q[k],v) for k,v in expected.items()) and strict(q['warp'],expected_warp(outputs)),'Exact claimed native profile geometry')

    check(request['workerSha256']==terminal['workerSha256']==WORKER and request['observerSha256']==terminal['observerSha256']==sha(files['native/observer.py']) and request['producerSha256']==sha(files['native/producer.py']),'Actual observer/producer/binary provenance')
    check(terminal['exitCode']==0 and terminal['exitCodeHex']=='00000000' and terminal['pid']>0 and terminal['seconds']<1 and not terminal['observationTimedOut'] and not terminal['outputLimited'] and not terminal['retirementPending'] and not terminal['inputError'],'Actual bounded v5 terminal')
    packets=[json.loads(s) for s in files[base+'worker.stdout'].decode().splitlines() if s.strip()]
    check(len(packets)==2 and packets[0]==terminal['ready'] and packets[1]==complete and complete['operation']==request['request']['operation'],'Actual v5 packet identity')
    check(complete['protocol']=='sc-stretch-render-v5' and complete['complete'] and complete['audioSha256']==sha(files[base+'audio.wav']) and complete['sourceSha256']==sha(files[base+'prototype-source.wav']),'Actual v5 publication hashes')
    source,sv=pcm(files[base+'prototype-source.wav']);data,values=pcm(files[base+'audio.wav']);prototype,pv=pcm(files[base+'prototype-rendered.wav'])
    target=32768 if profile=='identity' else 49152
    check(len(source)==32768*8 and len(data)==target*8 and complete['writtenFrames']==target and complete['sampleSha256']==sha(data),'Independent complete PCM geometry/digest')
    check(data==prototype and values==pv and max(map(abs,values))==complete['peakLinear']==1.5,'Retained independent prototype PCM/headroom')
    if profile=='identity':check(source==data,'Identity owned source copy')
    profiles.append(terminal['pid'])
check(len(set(profiles))==3,'Distinct owned v5 reproductions')

# Original uploaded archives are preserved byte for byte inside the outer capsule.
# Normalize only Windows ZIP separators in memory, reject paths before reading.
def capture(name):
    with ZipFile(BytesIO(files['installed/'+name])) as z:
        infos=z.infolist();check(len(infos)<100 and sum(i.file_size for i in infos)<5*1024*1024,'Bounded original installed archive')
        result={};prefix=None
        for item in infos:
            path=item.filename.replace('\\','/').rstrip('/');safe(path)
            parts=path.split('/',1);check(parts[0].startswith('sc-capture-') and len(parts)==2,'Owned original capture root')
            prefix=prefix or parts[0];check(parts[0]==prefix,'Single owned capture root')
            if item.is_dir():continue
            check(parts[1] not in result,'Unique normalized captured entry');result[parts[1]]=z.read(item)
        return result
setup=capture('diagnostic2.zip');first=capture('closed-20261010124948390.zip');second=capture('closed-20261010125851896.zip');history=capture('cceed-history.zip')
receipt=read('package/receipt.json');source=read('package/source-export-qualified.json')
check(receipt['sourceHead']==COMMIT and len(receipt['payload'])==64 and receipt['sequence']=='20261010122005','Matching package source/payload')
check(source['sourceHead']==COMMIT and source['sourceTree']==TREE and source['gitBlobCount']==2211 and source['allGitContentMatched'] and source['exportPolicy']['convertedPaths']==['.github/scripts/prepare-windows-desktop.ps1'] and not source['releaseUploaded'],'Corresponding source with declared CRLF export policy')
setup_artifact='SoundCurrent-DAW-20261010122005-cceedcc6e6d4-x64-setup.exe'
source_artifact='soundcurrent-daw-20261010122005-cceedcc6e6d4-source.tar.gz'
check(receipt['artifacts'][setup_artifact]['bytes']==35122342 and receipt['artifacts'][setup_artifact]['sha256']=='f68a47c80e11a70a978ef20b95e037de1600b5826abb0fd919ab2711bf19bde4' and receipt['artifacts'][source_artifact]['bytes']==801420853 and receipt['artifacts'][source_artifact]['sha256']==source['sourceArchiveSha256']=='7baa6d4e464a51af880834294a5e91d5bc166e0b310ec26a43ee09c61c33dc3a','Exact locally prepared installer/source binding')
check(receipt['payload']['soundcurrent-daw.exe']['sha256']==MAIN and receipt['payload']['sc-stretch-render-worker.exe']['sha256']==WORKER and receipt['cleanInstallQualified'] is False and receipt['nativeAudioReplayed'] is False and receipt['releaseUploaded'] is False,'Prepared package retains original qualification scope')

install=decode(setup['setup-result.json'])
check(install['sourceHead']==COMMIT and install['installerSha256']=='f68a47c80e11a70a978ef20b95e037de1600b5826abb0fd919ab2711bf19bde4' and install['setupExit']==install['exitCode']==0 and install['verifiedPayloadFiles']==64,'Actual exact installer terminal/payload checks')
check(install['vmUuid'].lower()=='670168f5-4e3f-4e7f-b218-3583db31f0f8' and install['sessionId']==1 and install['previousPreviewPresent'] and install['pristineOs'] is False and not install['compilerOnPath'] and not install['qtSdkOnPath'] and not install['nativeAudioActivated'],'Existing SDK-free independent clone scope')
check(len(install['shortcuts'])==2 and all(s['target'].endswith('20261010122005-cceedcc6e6d4\\soundcurrent-daw.exe') for s in install['shortcuts']),'Installed shortcuts')
main_ids=[]
for members,stamp in [(first,'20261010124948390'),(second,'20261010125851896')]:
    m=decode(members['main-'+stamp+'.json']);main_ids.append(m['pid'])
    check(m['sourceHead']==COMMIT and m['exeSha256']==MAIN and m['sessionId']==1 and m['windowHandle']>0 and m['closed'] and m['exitCode']==0 and not m['developerSdkUsed'] and not m['nativeAudioActivated'],'Actual installed normal close')
    for name in ('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','sndfile.dll','qwindows.dll'):
        modules=[v for v in m['modules'] if v['name']==name]
        check(len(modules)==1 and modules[0]['path'].lower().startswith(install['installLocation'].lower()+'\\'),'Actual installed local DLL binding')
check(len(set(main_ids))==2,'Fresh installed process reopen')
observer=decode(first['helper-observer.json'])
check(observer['stopped'] and observer['pollMilliseconds']==25 and not observer['traceRegistered'] and observer['traceError']=='Access denied ','Actual limited observer scope')
workers=[p for p in observer['processes'] if p['sha256']==WORKER]
check(len(workers)==1 and workers[0]['pid']==9192 and workers[0]['sessionId']==1 and workers[0]['path'].lower().startswith(install['installLocation'].lower()+'\\'),'Actual installed helper identity')
for name in ('sc-stretch-render-worker.exe','sndfile.dll'):
    modules=[v for v in workers[0]['modules'] if v['name']==name]
    check(len(modules)==1 and modules[0]['path'].lower().startswith(install['installLocation'].lower()+'\\'),'Actual installed helper local dependency')
project='project-été-Κиїв/project.json';raw_name='project-été-Κиїв/media/raw-été.wav';mix='project-été-Κиїв-mix.wav'
applied=decode(history['project-applied.json']);undo=decode(history['project-undo.json']);redo=decode(history['project-redo.json']);opened=decode(second[project]);initial=decode(setup[project])
clip=lambda s:s['tracks'][0]['clips'][0]
check(clip(applied)==clip(redo)==clip(opened) and clip(undo)['assetId']==clip(initial)['assetId'] and clip(undo)['lengthFrames']==32768 and clip(undo)['stretch'] is None,'Saved actual Apply/Undo/Redo/reopen states')
stretch=clip(opened)['stretch'];marker=stretch['warp']['markers']
check(clip(opened)['lengthFrames']==49152 and stretch['settings']=={'formantPreserved':True,'pitchMilliCents':0,'timeDenominator':2,'timeNumerator':3} and len(marker)==1 and marker[0]['source']==[4096,0,1] and marker[0]['output']==[6144,0,1],'Exact persisted protected controls')
check(first[raw_name]==second[raw_name]==setup[raw_name] and sha(first[raw_name])==stretch['sourceSha256'],'Untouched retained original source')
job='project-été-Κиїв/media/derived/'+clip(opened)['assetId']+'/'
check(first[job+'complete.json']==second[job+'complete.json'] and first[job+'audio.wav']==second[job+'audio.wav'],'Retained artifact unchanged across installed reopen')
complete=decode(first[job+'complete.json']);data,values=pcm(first[job+'audio.wav']);export,ev=pcm(first[mix]);repeat,rv=pcm(second[mix])
check(complete['protocol']=='sc-stretch-render-v5' and complete['complete'] and complete['renderKey']==stretch['renderKey'] and complete['sampleSha256']==sha(data),'Installed complete artifact/selection binding')
check(strict(complete['warp'],stretch['warp']) and complete['processor']==stretch['processor'] and complete['assetId']==stretch['sourceAssetId'] and complete['sourceSha256']==stretch['sourceSha256'] and complete['sourceFrames']==32768 and complete['frames']==stretch['sourceFrames']==32768 and complete['first']==stretch['sourceOrigin']['frame']==0 and complete['firstFraction']==stretch['sourceOrigin']['fraction']==0 and complete['firstDenominator']==stretch['sourceOrigin']['denominator']==1 and complete['sourceAlgorithm']==stretch['sourceOrigin']['algorithm'] and strict(complete['pitchMilliCents'],stretch['settings']['pitchMilliCents']) and strict(complete['formantPreserved'],stretch['settings']['formantPreserved']) and complete['rate']==48000 and complete['channels']==2 and complete['target']==complete['writtenFrames']==clip(opened)['lengthFrames']==49152,'Installed completion/persisted geometry binding')

check(len(data)==len(export)==49152*8 and values==ev==rv and export==repeat and max(map(abs,values))==max(map(abs,ev))==1.5,'Whole installed exported PCM values/headroom and repeated export')
# Mixing accumulation turns -0 into +0; no nonzero sample differs.
zero_sign_changes=sum(x!=y for x,y in zip(struct.iter_unpack('<I',data),struct.iter_unpack('<I',export)))
check(zero_sign_changes>0 and all(a==b for a,b in zip(values,ev)),'Only signed-zero PCM representation changes')
print(json.dumps({'checks':checks,'nativeV5Workers':33,'nativeV5Checks':519,'retainedWholePcmProfiles':3,'installedProcesses':main_ids,'installedFrames':49152,'peak':1.5,'zeroSignChanges':zero_sign_changes,'pristineOs':False,'causeResolved':False,'nativeReplay':False,'physicalAudio':False,'releaseUploaded':False}))
