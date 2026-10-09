#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual isolated stretch worker, owned WAVs and independent RF64/sample checks."""
from pathlib import Path
import hashlib,json,math,struct,subprocess,sys,tempfile,uuid,time
worker=Path(sys.argv[1]).resolve()
checks=0

def check(value,message):
    global checks
    checks+=1
    if not value:raise AssertionError(message)

def owned_wave(path,rate,channels,frames):
    payload=b''.join(struct.pack('<f',1.8*math.sin(2*math.pi*(1000+ch*500)*f/rate)) for f in range(frames) for ch in range(channels))
    fmt=struct.pack('<HHIIHH',3,channels,rate,rate*channels*4,channels*4,32)
    path.write_bytes(b'RIFF'+struct.pack('<I',36+len(payload))+b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload)
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read_rf64(path):
    raw=path.read_bytes();check(raw[:4]==b'RF64' and raw[8:12]==b'WAVE','Not an actual RF64')
    at=12;data_size=None;data=None;fmt=None;encoding=None
    while at+8<=len(raw):
        name=raw[at:at+4];size=struct.unpack_from('<I',raw,at+4)[0];at+=8
        if name==b'data' and size==0xffffffff:
            check(data_size is not None,'No RF64 data size');size=data_size
        check(size<=len(raw)-at,'Truncated RF64 chunk')
        if name==b'ds64':data_size=struct.unpack_from('<Q',raw,at+8)[0]
        if name==b'fmt ':
            fmt=struct.unpack_from('<HHIIHH',raw,at);encoding=fmt[0]
            if encoding==0xfffe:
                check(size>=40 and struct.unpack_from('<H',raw,at+16)[0]==22 and struct.unpack_from('<H',raw,at+18)[0]==32,'Invalid extensible precision')
                check(raw[at+24:at+40]==bytes.fromhex('0300000000001000800000aa00389b71'),'Unexpected RF64 subtype GUID');encoding=3
        if name==b'data':data=raw[at:at+size]
        at+=size+(size&1)
    check(fmt is not None and encoding==3 and fmt[-1]==32 and data is not None,'RF64 float encoding differs')
    return fmt[1],fmt[2],data

def amplitude(samples,channels,ch,rate,hz):
    count=len(samples)//channels;start=count//3;end=2*count//3
    real=imag=0.
    for n in range(start,end):
        x=samples[n*channels+ch];angle=2*math.pi*hz*n/rate
        real+=x*math.cos(angle);imag+=x*math.sin(angle)
    return 2*math.hypot(real,imag)/(end-start)

with tempfile.TemporaryDirectory(prefix='sc-stretch-owned-') as temporary:
    root=Path(temporary)/'studio Δ';root.mkdir();(root/'media').mkdir();jobs=root/'media/derived';jobs.mkdir()
    path=root/'media/tone.wav';digest=owned_wave(path,48000,2,12000)
    initial={'protocol':'sc-stretch-render-v1','operation':str(uuid.uuid4()),'assetId':str(uuid.uuid4()),'relative':'media/tone.wav','sha256':digest,'rate':48000,'channels':2,'sourceFrames':12000,'first':0,'frames':12000,'timeNumerator':3,'timeDenominator':2,'pitchMilliCents':1200000,'formantPreserved':False}
    def command(memory=256,maximum=1000000,bytes_limit=64*1024*1024,deadline=10000):
        return [str(worker),'--project-root',str(root),'--jobs-root',str(jobs),'--memory-mib',str(memory),'--maximum-input-frames',str(maximum),'--maximum-output-bytes',str(bytes_limit),'--deadline-ms',str(deadline)]
    def run(request,**limits):
        raw=request if isinstance(request,str) else json.dumps(request)
        process=subprocess.Popen(command(**limits),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        process.stdin.write(raw);process.stdin.close()
        first=process.stdout.readline();reports=[json.loads(first)] if first else []
        if reports and reports[0].get('event')=='ready':
            (jobs/reports[0]['operation']/'start.request').write_text('start\n')
        output=process.stdout.read();error=process.stderr.read();process.wait(timeout=15)
        reports.extend(json.loads(line) for line in output.splitlines())
        return subprocess.CompletedProcess(command(**limits),process.returncode,first+output,error),reports
    def fresh(**changes):
        value=dict(initial,operation=str(uuid.uuid4()));value.update(changes);return value
    result,reports=run(initial);check(result.returncode==0,'Actual stretch failed: '+result.stderr)
    receipt=reports[-1];job=jobs/initial['operation'];check(receipt['complete'] and receipt['writtenFrames']==18000,'Exact independent duration failed')
    check(json.loads((job/'complete.json').read_text())==receipt,'Published completion receipt differs')
    channels,rate,data=read_rf64(job/'audio.wav');check(channels==2 and rate==48000 and len(data)==18000*2*4,'RF64 actual geometry differs')
    check(hashlib.sha256(data).hexdigest()==receipt['sampleSha256'],'Independent sample digest differs');check(hashlib.sha256((job/'audio.wav').read_bytes()).hexdigest()==receipt['audioSha256'],'Actual audio hash differs')
    samples=struct.unpack('<'+'f'*(len(data)//4),data);check(all(math.isfinite(x) for x in samples),'Nonfinite actual output');check(max(abs(x) for x in samples)>1,'Output clipped float headroom')
    check(amplitude(samples,2,0,48000,2000)>1 and amplitude(samples,2,1,48000,3000)>1,'Independent pitch or channel ordering differs')
    check(amplitude(samples,2,0,48000,1000)<.03,'Pitch left the original frequency')
    check(hashlib.sha256(path.read_bytes()).hexdigest()==digest,'Raw source changed')
    artifact=subprocess.run([sys.argv[2],str(root),'media/derived/'+initial['operation']+'/audio.wav'],text=True,capture_output=True,timeout=15)
    check(artifact.returncode==0,'Shared live/export workflow failed: '+artifact.stderr);print(artifact.stdout.strip())
    repeated=fresh();r,rr=run(repeated);check(r.returncode==0,'Repeat job failed');check(rr[-1]['renderKey']==receipt['renderKey'] and rr[-1]['audioSha256']==receipt['audioSha256'],'Repeated render identity/waveform differs')
    # Exact ceil counterexample from the candidate experiment, plus source anchor.
    short=fresh(first=17,frames=4816,timeNumerator=4,timeDenominator=3,pitchMilliCents=0)
    r,rr=run(short);check(r.returncode==0 and rr[-1]['writtenFrames']==6422,'Exact integer target drifted to vendor nearest rounding')
    for changes in [{'rate':384000},{'frames':12001},{'timeDenominator':0},{'timeNumerator':True},{'pitchMilliCents':18446744073707151616},{'sha256':'0'*64},{'relative':'../media/tone.wav'},{'channels':0},{'formantPreserved':1},{'pitchMilliCents':2400001},{'first':11999,'frames':2}]:
        req=fresh(**changes);r,rr=run(req);check(r.returncode!=0 and not (jobs/req['operation']/'complete.json').exists(),'Invalid source/parameters published completion');check(not (jobs/req['operation']).exists(),'Refusal occurred after destination mutation')
    duplicate=json.dumps(fresh()).replace('"timeNumerator": 3','"timeNumerator": 3, "timeNumerator": 4')
    r,rr=run(duplicate);check(r.returncode!=0,'Duplicate request key accepted')
    req=fresh();r,rr=run(req,bytes_limit=4096);check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Short disk grant performed destination IO')
    req=fresh();r,rr=run(req,maximum=100);check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Short input grant performed destination IO')
    r,rr=run(initial);check(r.returncode!=0 and json.loads((job/'complete.json').read_text())==receipt,'Existing job overwritten')
    # Cancel a genuinely running owned job after its ready event.
    digest_long=owned_wave(path,48000,2,48000);long=fresh(sha256=digest_long,sourceFrames=48000,frames=48000)
    process=subprocess.Popen(command(),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    process.stdin.write(json.dumps(long));process.stdin.close();ready=json.loads(process.stdout.readline());check(ready['event']=='ready','No owned cancellation boundary')
    (jobs/long['operation']/'cancel.request').write_text('cancel\n');out=process.stdout.read();err=process.stderr.read();process.wait(timeout=15)
    check(process.returncode!=0 and not (jobs/long['operation']/'complete.json').exists(),'Canceled actual render published completion')
    # Observe a real in-progress WAV and cancel after processing starts.
    digest_long=owned_wave(path,48000,2,480000);req=fresh(sha256=digest_long,sourceFrames=480000,frames=480000)
    process=subprocess.Popen(command(),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    process.stdin.write(json.dumps(req));process.stdin.close();ready=json.loads(process.stdout.readline());active_job=jobs/req['operation']
    (active_job/'start.request').write_text('start\n');until=time.monotonic()+5
    while not (active_job/'audio.partial').exists() and process.poll() is None and time.monotonic()<until:time.sleep(.001)
    check((active_job/'audio.partial').exists() and process.poll() is None,'Actual render did not reach live processing boundary')
    (active_job/'cancel.request').write_text('cancel\n');out=process.stdout.read();err=process.stderr.read();process.wait(timeout=15)
    check(process.returncode!=0 and not (active_job/'complete.json').exists(),'In-progress canceled render published completion')
    # Deadline terminates an active heavy render, including opaque vendor work.
    req=fresh(sha256=digest_long,sourceFrames=480000,frames=480000)
    before=time.monotonic();r,rr=run(req,deadline=100);elapsed=time.monotonic()-before
    check(r.returncode==2 and elapsed<3 and not (jobs/req['operation']/'complete.json').exists(),'Hard deadline failed to retire uncommitted job')
    # Opaque workspace exceeds an OS-enforced child ceiling, not a guessed allowance.
    digest_wide=owned_wave(path,192000,32,128);req=fresh(sha256=digest_wide,rate=192000,channels=32,sourceFrames=128,frames=128)
    r,rr=run(req,memory=32);check(r.returncode!=0 and not (jobs/req['operation']/'complete.json').exists(),'Opaque vendor workspace bypassed memory ceiling')
    print(json.dumps({'checks':checks,'actualOwnedCompletedJobs':3,'headroom':True,'exactCeilCounterexample':6422,'cooperativeCancel':True,'midRenderCancel':True,'hardDeadline':True,'OSMemoryRefusal':True,'nativePlatform':sys.platform,'nativeAudio':False}))
