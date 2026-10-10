#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect bounded retained observations; never execute Windows/audio payloads."""
import hashlib,json,math,struct
from pathlib import Path
root=Path(__file__).resolve().parent
checks=0
def check(value,why):
    global checks
    checks+=1
    if not value:raise AssertionError(why)
def read(name):return json.loads((root/name).read_text(encoding='utf-8-sig'))
def digest(raw):return hashlib.sha256(raw).hexdigest()
manifest=read('manifest.json')
check(manifest['format']=='sc-windows-bounded-observation-v1','Observation format')
expected={'before.json','terminal.json','worker.stdout','worker.stderr','v4-qualification.json','main-open-close.json','native-v4.log','native-v5-failed.log','linux-observation-tests.log','linux-stale-worker-refusal.log','raw-tone.wav','rendered-tone.wav','complete.json','intent.json'}
check(set(manifest['members'])==expected,'Retained membership')
for name,binding in manifest['members'].items():
    path=root/name
    check(path.is_file() and not path.is_symlink(),'Plain captured member '+name)
    check(path.stat().st_size==binding['bytes'] and binding['bytes']<=1024*1024,'Bounded member '+name)
    check(digest(path.read_bytes())==binding['sha256'],'Exact capture '+name)
commit='8b46e8975e832edc3fc8c19b1c62d3edf96277e2';tree='fe3197c30e20c30493f03eb9ba9720a12f7de2e2'
for name in ('manifest.json','before.json','terminal.json','v4-qualification.json','main-open-close.json'):
    item=read(name);check(item['sourceCommit']==commit and item['sourceTree']==tree,'Historical source binding '+name)
check(manifest['nativeV4Completed'] and manifest['nativeDesktopOpenCloseCompleted'],'Bounded successful observations')
for field in ('nativeV5Completed','installedPreviewQualified','causeResolved','physicalAudio','fullQualityQualified'):
    check(manifest[field] is False,'Unqualified scope remains explicit '+field)
before=read('before.json');terminal=read('terminal.json');v4=read('v4-qualification.json');complete=read('complete.json');main=read('main-open-close.json')
check(not before['sourceModified'],'Native source clean before single request')
check(before['workerSha256']==terminal['workerSha256']==v4['exeSha256']=='514bee45bc78a8c7b0173753c92bd6934c64010c9191be867b060f86f850ce7d','Exact previously compiled worker identity')
check(terminal['exitCode']==0 and terminal['exitCodeHex']=='00000000' and terminal['ready'] and not terminal['forcedRetirement'],'Actual single request terminal')
check(0<terminal['seconds']<1 and terminal['pid']>0 and not terminal['nativeAudio'],'Bounded owned worker observation')
packets=[json.loads(line) for line in (root/'worker.stdout').read_text().splitlines()]
check(len(packets)==2 and packets[0]['event']=='ready' and packets[1]==complete,'Actual ready/completion packets')
check([json.loads(item['line']) for item in terminal['trace']]==packets,'Captured timed protocol trace')
check((root/'worker.stderr').read_bytes()==b'','Actual empty stderr')
check(before['request']['operation']==packets[0]['operation']==complete['operation'],'Owned ready acknowledgment')
check(complete['protocol']=='sc-stretch-render-v4' and complete['complete'] and complete['writtenFrames']==18000 and complete['channels']==2 and complete['rate']==48000,'Exact actual render geometry')
check(before['request']['timeNumerator']==3 and before['request']['timeDenominator']==2 and complete['target']==18000 and complete['pitchMilliCents']==before['request']['pitchMilliCents']==1200000,'Actual pitch/duration request')
check(complete['sourceSha256']==before['request']['sha256']==digest((root/'raw-tone.wav').read_bytes()),'Owned raw source hash')
check(complete['audioSha256']==digest((root/'rendered-tone.wav').read_bytes())==v4['audioSha256'],'Independent runs produced identical derivative bytes')
check(v4['operation']!=complete['operation'] and v4['pid']!=terminal['pid'],'Separate actual worker requests')
check(v4['nativePlatform']=='win32' and v4['checks']==385 and v4['verifierChecks']==1045 and v4['verifiedArtifact'] and v4['exitCode']==0 and v4['verifierExitCode']==0 and not v4['nativeAudio'],'Actual v4 bank and shared workflow verifier')
check(main['exeSha256']=='f4e50623f7e5e204f9ce5695304bad77ef7f145333c4d7c2b266904435de1ed7','Exact previously compiled desktop identity')
check(main['pid']>0 and main['sessionId']==1 and main['windowHandle']>0 and main['title']=='SoundCurrent DAW' and main['normalClose'] and not main['forcedRetirement'] and main['exitCode']==0,'Interactive startup and ordinary close')
check(not main['styleOverride'] and not main['nativeAudio'],'Default style and no audio route activation')

def pcm(name):
    raw=(root/name).read_bytes();check(raw[:4] in (b'RIFF',b'RF64') and raw[8:12]==b'WAVE','Owned float WAV envelope')
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
raw,raw_values=pcm('raw-tone.wav');derived,values=pcm('rendered-tone.wav')
check(len(raw)==12000*8 and len(derived)==18000*8,'Independent whole frame counts')
check(digest(derived)==complete['sampleSha256']==v4['sampleSha256'],'Complete PCM digest')
check(max(map(abs,raw_values))>1 and max(map(abs,values))==complete['peakLinear']>1,'Actual floating-point headroom')
failed=(root/'native-v5-failed.log').read_text(errors='replace')
check('subprocess.TimeoutExpired' in failed and 'protected_warp_worker_tests.py' in failed,'Unresolved original v5 timeout retained')
linux=(root/'linux-observation-tests.log').read_text()
check('100% tests passed, 0 tests failed out of 3' in linux and 'worker-observation-failures' in linux,'Linux observer and both real render banks')
print(json.dumps({'checks':checks,'retainedNativeV4':True,'retainedDesktopOpenClose':True,'nativeV5Qualified':False,'installedPreviewQualified':False,'causeResolved':False,'nativeReplay':False}))
