#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Owned v5 workers compared to unchanged PR89 full PCM; no listening/quality claim."""
import os
import argparse, hashlib, json, shutil, struct, subprocess, tempfile, uuid, time, platform
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('worker',type=Path);p.add_argument('verifier',type=Path);p.add_argument('prototype',type=Path)
p.add_argument('--qualification',type=Path,default=os.environ.get('SC_PROTECTED_WARP_QUALIFICATION'))
a=p.parse_args();worker=a.worker.resolve();verifier=a.verifier.resolve();prototype=a.prototype.resolve()
checks=renders=0
records=[];comparisons=[];started=time.monotonic()
sourceMutation={}
source=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git',*args],cwd=source,text=True).strip()
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
if a.qualification:
    if a.qualification.exists() or git('status','--porcelain','--untracked-files=no'):raise RuntimeError('Qualification needs committed clean source and a fresh report path')
    binding={'sourceCommit':git('rev-parse','HEAD'),'sourceTree':git('rev-parse','HEAD^{tree}'),'workerSha256':digest(worker),'verifierSha256':digest(verifier),'prototypeSha256':digest(prototype)}

def check(v,why):
    global checks
    checks+=1
    if not v:raise AssertionError(why)

def pcm(path):
    raw=path.read_bytes();check(raw[:4] in (b'RIFF',b'RF64') and raw[8:12]==b'WAVE','Actual owned WAV envelope')
    at=12;size64=None;data=None
    while at+8<=len(raw):
        name=raw[at:at+4];size=struct.unpack_from('<I',raw,at+4)[0];at+=8
        if name==b'ds64':size64=struct.unpack_from('<Q',raw,at+8)[0]
        if name==b'data' and size==0xffffffff:check(size64 is not None,'RF64 ds64 required');size=size64
        check(size<=len(raw)-at,'Actual WAV chunk extent')
        if name==b'data':data=raw[at:at+size]
        at+=size+(size&1)
    check(data is not None,'Complete PCM data required');return data

def normalized(events,targets,frames=32768,target=49152):
    markers=[];spans=[];points=[{'source':[0,0,1],'output':[0,0,1]}]
    for i,(source,output) in enumerate(zip(events,targets)):
        owner=f'00000000-0000-0000-0000-{i+1:012x}'
        markers.append({'id':owner,'source':[source,0,1],'output':[output,0,1]})
        span={'owner':owner}
        for name,value in [('sourceBegin',source-256),('sourceAnchor',source),('sourceEnd',source+2048),('outputBegin',output-256),('outputAnchor',output),('outputEnd',output+2048)]:span[name]=[value,0,1]
        spans.append(span)
        for x,y in [(source-256,output-256),(source,output),(source+2048,output+2048)]:
            point={'source':[x,0,1],'output':[y,0,1]}
            if point!=points[-1]:points.append(point)
    last={'source':[frames,0,1],'output':[target,0,1]}
    if points[-1]!=last:points.append(last)
    return {'mode':'transient-protected-v1','before':256,'after':2048,'halo':64,'minimumNonunityGap':64,'chunkFrames':512,'map':'soundcurrent.warp-piecewise-rational-v1','markers':markers,'spans':spans,'points':points}

with tempfile.TemporaryDirectory(prefix='sc-protected-owned-') as temporary:
    root=Path(temporary)/'studio été';root.mkdir();(root/'media').mkdir();jobs=root/'media/derived';jobs.mkdir()
    path=root/'media/source.wav'
    def request(channels,profile,events=[4096,12288,20480,28672],targets=None):
        target=32768 if profile=='identity' else 49152
        if targets is None:targets=events if profile=='identity' else [v*3//2 for v in events] if profile=='uniform' else [6144,16384,32768,43008]
        return {'protocol':'sc-stretch-render-v5','processor':'soundcurrent.stretch-transient-protected-rubberband4-r3-v1','operation':str(uuid.uuid4()),'assetId':str(uuid.uuid4()),'relative':'media/source.wav','sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'rate':48000,'channels':channels,'sourceFrames':32768,'first':0,'firstFraction':0,'firstDenominator':1,'frames':32768,'timeNumerator':1 if profile=='identity' else 3,'timeDenominator':1 if profile=='identity' else 2,'pitchMilliCents':0,'formantPreserved':True,'contextEnabled':False,'contextBefore':0,'contextAfter':0,'warp':normalized(events,targets,target=target)}
    def run(q,after_ready=None):
        command=[str(worker),'--project-root',str(root),'--jobs-root',str(jobs),'--memory-mib','256','--maximum-input-frames','100000','--maximum-output-bytes',str(8*1024*1024),'--deadline-ms','10000']
        child=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            child.stdin.write(json.dumps(q));child.stdin.close();child.stdin=None
            first=child.stdout.readline();ready=json.loads(first) if first else None
            if ready and ready.get('event')=='ready':
                if after_ready:after_ready(jobs/ready['operation'])
                (jobs/ready['operation']/'start.request').write_text('start')
            out,err=child.communicate(timeout=12)
            reports=[json.loads(line) for line in out.splitlines()]
            records.append({'pid':child.pid,'exit':child.returncode,'request':q,'ready':ready,'completion':reports,'stderr':err})
            return child.returncode,ready,reports,err
        finally:
            if child.poll() is None:child.kill();child.communicate(timeout=3)
    for family in ['impulse','attack','long-attack','attack-bed','sustain','cancellation']:
        for profile in ['identity','uniform','nonuniform']:
            folder=root/f'{family}-{profile}'
            probe=subprocess.run([str(prototype),json.dumps({'family':family,'profile':profile}),str(folder)],capture_output=True,text=True,timeout=10)
            check(probe.returncode==0 and not probe.stderr,'Unchanged PR89 prototype actually completed')
            shutil.copyfile(folder/'source.wav',path);q=request(2 if family=='impulse' else 8,profile)
            code,ready,reports,err=run(q);renders+=1
            check(code==0 and not err and ready['protocol']=='sc-stretch-render-v5','Actual v5 worker completed')
            job=jobs/q['operation'];actual=pcm(job/'audio.wav');reference=pcm(folder/'rendered.wav')
            check(actual==reference,'Complete streaming PCM differs from retained prototype '+family+'/'+profile)
            comparisons.append({'family':family,'profile':profile,'sourcePcmSha256':hashlib.sha256(pcm(path)).hexdigest(),'prototypePcmSha256':hashlib.sha256(reference).hexdigest(),'workerPcmSha256':hashlib.sha256(actual).hexdigest(),'operation':q['operation'],'channels':q['channels'],'frames':len(actual)//(q['channels']*4),'allPcmEqual':actual==reference,'prototypeReport':json.loads(probe.stdout)})
            receipt=json.loads((job/'complete.json').read_text());check(receipt==reports[-1] and receipt['warp']==q['warp'],'Complete normalized identity differs')
            check(hashlib.sha256(actual).hexdigest()==receipt['sampleSha256'],'Independent whole PCM hash')
            size=q['channels']*4;raw=pcm(path)
            for span in q['warp']['spans']:
                sb,se,ob,oe=[span[n][0] for n in ['sourceBegin','sourceEnd','outputBegin','outputEnd']]
                check(actual[(ob+64)*size:(oe-64)*size]==raw[(sb+64)*size:(se-64)*size],'Every protected core exact bytes')
            if family in ['impulse','cancellation'] and profile=='nonuniform':
                verification=root/('verify-'+q['operation']);(verification/'media').mkdir(parents=True);(verification/'media/derived').mkdir()
                shutil.copyfile(path,verification/'media/source.wav');shutil.copytree(job,verification/'media/derived'/q['operation'])
                verified=subprocess.run([str(verifier),str(verification),f'media/derived/{q["operation"]}/audio.wav',json.dumps(q),json.dumps(ready)],capture_output=True,text=True,timeout=20)
                check(verified.returncode==0 and not verified.stderr,'Actual v5 parent adoption/live/export/reopen verifier: '+verified.stderr)
                check('callback_audit=0' in verified.stdout,'Actual v5 RT audit required')
    # Distinct geometry edges absent from the frozen regular bank.
    for events,targets in [([256,30720],[256,47104]),([4096,6400],[6144,8448])]:
        q=request(8,'nonuniform',events,targets);code,ready,reports,err=run(q);renders+=1
        check(code==0 and reports[-1]['complete'] and not err,'Endpoint/touching protected geometry')
    # Raw origin is applied exactly once by the shared source kernel.
    original=path.read_bytes();prefixed=bytearray(original[:44]+bytes(17*8*4)+original[44:]);struct.pack_into('<I',prefixed,4,len(prefixed)-8);struct.pack_into('<I',prefixed,40,len(prefixed)-44);path.write_bytes(prefixed)
    shifted=request(8,'nonuniform');shifted['first']=17;shifted['sourceFrames']=32785
    code,ready,reports,err=run(shifted);renders+=1
    check(code==0 and not err and pcm(jobs/shifted['operation']/'audio.wav')==pcm(folder/'rendered.wav'),'Integer raw origin was skipped or applied twice')
    path.write_bytes(original)
    short=request(8,'nonuniform',[320],[384]);code,ready,reports,err=run(short)
    check(code!=0 and ready is None and not (jobs/short['operation']).exists(),'Short acoustic gap was repaired or published')
    def cancel_owned_job(job):
        if os.name=='nt':
            before=path.read_bytes()
            try:path.write_bytes(before+b'changed')
            except PermissionError as error:
                unchanged=path.read_bytes()==before
                check(unchanged,'Denied Windows source mutation changed raw bytes')
                sourceMutation['heldReadWriteDenied']={'errno':error.errno,'winerror':getattr(error,'winerror',None),'rawUnchanged':unchanged}
            else:raise AssertionError('Windows approved read handle admitted raw source mutation')
        (job/'cancel.request').write_text('cancel')
    canceled=request(8,'nonuniform');code,ready,reports,err=run(canceled,cancel_owned_job)
    check(code!=0 and (jobs/canceled['operation']/'intent.json').exists() and not (jobs/canceled['operation']/'complete.json').exists(),'Canceled v5 job did not retain incomplete intent')
    original=path.read_bytes();changed=request(8,'nonuniform')
    if os.name=='nt':
        path.write_bytes(original+b'changed')
        code,ready,reports,err=run(changed)
        check(ready is None and not (jobs/changed['operation']).exists(),'Stale Windows raw hash created a v5 job')
        sourceMutation['changedRawTiming']='before-read'
    else:
        code,ready,reports,err=run(changed,lambda job:path.write_bytes(original+b'changed'))
        sourceMutation['changedRawTiming']='after-ready'
    check(code!=0 and not (jobs/changed['operation']/'complete.json').exists(),'Changed raw source acquired a completed v5 artifact')
    path.write_bytes(original)
    base=request(8,'nonuniform')
    for mode in range(9):
        q=json.loads(json.dumps(base));q['operation']=str(uuid.uuid4())
        if mode==0:q['warp']['spans'][0]['sourceBegin'][0]+=1
        if mode==1:q['warp']['points'][0]['source'][0]=0.0
        if mode==2:q['warp']['markers'][1]['id']=q['warp']['markers'][0]['id']
        if mode==3:q['firstFraction']=1;q['firstDenominator']=2
        if mode==4:q['pitchMilliCents']=1
        if mode==5:q['contextEnabled']=True
        if mode==6:q['warp']['mode']='unknown'
        if mode==7:q['warp']['halo']=0
        if mode==8:q['warp']['markers'][0]['source'][2]=0
        code,ready,reports,err=run(q)
        check(code!=0 and not (jobs/q['operation']).exists(),'Invalid v5 request created an owned job')
if a.qualification:
    check(binding['sourceCommit']==git('rev-parse','HEAD') and binding['sourceTree']==git('rev-parse','HEAD^{tree}') and not git('status','--porcelain','--untracked-files=no'),'Source identity changed during qualification')
    check(all(binding[k]==digest(path) for k,path in [('workerSha256',worker),('verifierSha256',verifier),('prototypeSha256',prototype)]),'Executable identity changed during qualification')
    report={'schema':'soundcurrent.protected-warp-integration-evidence-v1',**binding,'platform':platform.platform(),'workerProcesses':len(records),'completedRenders':renders,'checks':checks,'elapsedSeconds':time.monotonic()-started,'comparisons':comparisons,'workers':records,'sourceMutation':sourceMutation,'nativeAudio':False,'fullQualityQualified':False,'syntheticSourceRights':'Original project fixtures, GPL-3.0-only','minimumNonunityAcousticGapFrames':2048}
    a.qualification.parent.mkdir(parents=True,exist_ok=True);a.qualification.write_text(json.dumps(report,indent=2)+'\n')
print(f'protected_worker_renders={renders} checks={checks} frozen_full_pcm_equal=18 parent_live_export_reopen=2 quality_qualified=false')
