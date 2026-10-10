#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual isolated stretch worker, owned WAVs and independent RF64/sample checks."""
from pathlib import Path
import argparse,hashlib,json,math,os,re,struct,subprocess,sys,tempfile,uuid,time
from worker_observation import observe, diagnostic
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('worker',type=Path)
parser.add_argument('verifier',type=Path)
parser.add_argument('--qualification',type=Path,
                    default=os.environ.get('SC_STRETCH_WORKER_QUALIFICATION'))
args=parser.parse_args()
worker=args.worker.resolve();verifier=args.verifier.resolve()
root_source=Path(__file__).resolve().parents[1]
def git(*command):
    return subprocess.check_output(['git',*command],cwd=root_source,text=True).strip()
def executable_hash(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
if args.qualification:
    if args.qualification.exists():raise RuntimeError('Qualification output already exists')
    if git('status','--porcelain','--untracked-files=no'):raise RuntimeError('Commit the qualification source first')
    source_commit=git('rev-parse','HEAD');source_tree=git('rev-parse','HEAD^{tree}')
    worker_hash=executable_hash(worker);verifier_hash=executable_hash(verifier)
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
    initial={'protocol':'sc-stretch-render-v4','processor':'soundcurrent.stretch-rubberband4-r3-positioned-v2','operation':str(uuid.uuid4()),'assetId':str(uuid.uuid4()),'relative':'media/tone.wav','sha256':digest,'rate':48000,'channels':2,'sourceFrames':12000,'first':0,'firstFraction':0,'firstDenominator':1,'frames':12000,'timeNumerator':3,'timeDenominator':2,'pitchMilliCents':1200000,'formantPreserved':False,'contextEnabled':False,'contextBefore':0,'contextAfter':0}
    def command(memory=256,maximum=1000000,bytes_limit=64*1024*1024,deadline=10000):
        return [str(worker),'--project-root',str(root),'--jobs-root',str(jobs),'--memory-mib',str(memory),'--maximum-input-frames',str(maximum),'--maximum-output-bytes',str(bytes_limit),'--deadline-ms',str(deadline)]
    def run(request,**limits):
        raw=request if isinstance(request,str) else json.dumps(request)
        acknowledged=False
        def ready(line):
            nonlocal acknowledged
            try: packet=json.loads(line)
            except json.JSONDecodeError:return
            if packet.get('event')=='ready' and not acknowledged:
                expected=json.loads(raw)['operation']
                if packet.get('operation')!=expected:raise AssertionError('Ready operation differs from owned request')
                (jobs/expected/'start.request').write_text('start\n');acknowledged=True
        completed=observe(command(**limits),raw,ready,
                          timeout=max(15,limits.get('deadline',10000)/1000+5))
        if completed.observation_timed_out or completed.output_limited or completed.retirement_pending:
            raise AssertionError('Owned worker observation failed: '+diagnostic(completed))
        try:reports=[json.loads(line) for line in completed.stdout.splitlines()]
        except json.JSONDecodeError as error:
            raise AssertionError('Invalid owned worker output: '+diagnostic(completed)) from error
        if completed.returncode!=0:print('Owned v4 request '+raw+' outcome '+diagnostic(completed),flush=True)
        return completed,reports
    def fresh(**changes):
        value=dict(initial,operation=str(uuid.uuid4()));value.update(changes)
        unity=value['timeNumerator']==value['timeDenominator'] and value['pitchMilliCents']==0
        value['processor']=('soundcurrent.stretch-positioned-copy-region-v1' if unity else 'soundcurrent.stretch-rubberband4-r3-region-v1') if value['contextEnabled'] else ('soundcurrent.stretch-positioned-copy-v1' if unity else 'soundcurrent.stretch-rubberband4-r3-positioned-v2')
        return value
    result,reports=run(initial);check(result.returncode==0,'Actual stretch failed: '+diagnostic(result))
    receipt=reports[-1];job=jobs/initial['operation'];check(receipt['complete'] and receipt['writtenFrames']==18000,'Exact independent duration failed')
    check(json.loads((job/'complete.json').read_text())==receipt,'Published completion receipt differs')
    channels,rate,data=read_rf64(job/'audio.wav');check(channels==2 and rate==48000 and len(data)==18000*2*4,'RF64 actual geometry differs')
    check(hashlib.sha256(data).hexdigest()==receipt['sampleSha256'],'Independent sample digest differs');check(hashlib.sha256((job/'audio.wav').read_bytes()).hexdigest()==receipt['audioSha256'],'Actual audio hash differs')
    samples=struct.unpack('<'+'f'*(len(data)//4),data);check(all(math.isfinite(x) for x in samples),'Nonfinite actual output');check(max(abs(x) for x in samples)>1,'Output clipped float headroom')
    check(amplitude(samples,2,0,48000,2000)>1 and amplitude(samples,2,1,48000,3000)>1,'Independent pitch or channel ordering differs')
    check(amplitude(samples,2,0,48000,1000)<.03,'Pitch left the original frequency')
    check(hashlib.sha256(path.read_bytes()).hexdigest()==digest,'Raw source changed')
    artifact_process=subprocess.Popen([str(verifier),str(root),'media/derived/'+initial['operation']+'/audio.wav',json.dumps(initial),json.dumps(reports[0])],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    try:artifact_out,artifact_err=artifact_process.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        artifact_process.kill();artifact_process.communicate();raise
    artifact=subprocess.CompletedProcess(artifact_process.args,artifact_process.returncode,artifact_out,artifact_err)
    check(artifact.returncode==0,'Shared live/export workflow failed: '+artifact.stderr);print(artifact.stdout.strip())
    repeated=fresh();r,rr=run(repeated);check(r.returncode==0,'Repeat job failed');check(rr[-1]['renderKey']==receipt['renderKey'] and rr[-1]['audioSha256']==receipt['audioSha256'],'Repeated render identity/waveform differs')
    # Exact ceil counterexample from the candidate experiment, plus source anchor.
    short=fresh(first=17,frames=4816,timeNumerator=4,timeDenominator=3,pitchMilliCents=0)
    r,rr=run(short);check(r.returncode==0 and rr[-1]['writtenFrames']==6422,'Exact integer target drifted to vendor nearest rounding')
    # Actual neutral renders isolate exact fractional source phase from pitch/time processing.
    base=fresh(first=2048,frames=4096,timeNumerator=1,timeDenominator=1,pitchMilliCents=0)
    r,rr=run(base);check(r.returncode==0,'Neutral integer anchor failed')
    floor_receipt=rr[-1];_,_,floor_data=read_rf64(jobs/base['operation']/'audio.wav')
    check(floor_receipt['processor']=='soundcurrent.stretch-positioned-copy-v1' and
          floor_data==path.read_bytes()[44+2048*8:44+(2048+4096)*8],
          'Unity integer render did not preserve exact source sample bits')
    half=dict(base,operation=str(uuid.uuid4()),firstFraction=1,firstDenominator=2)
    r,rr=run(half);check(r.returncode==0,'Actual fractional origin failed');half_receipt=rr[-1]
    _,_,half_data=read_rf64(jobs/half['operation']/'audio.wav')
    check(half_receipt['firstFraction']==1 and half_receipt['firstDenominator']==2,'Fractional origin lost in completion')
    check(half_receipt['sourceAlgorithm']=='soundcurrent.src-positioned-best-v1','Fractional source algorithm not bound')
    floor_samples=struct.unpack('<'+'f'*(len(floor_data)//4),floor_data)
    half_samples=struct.unpack('<'+'f'*(len(half_data)//4),half_data)
    def tone_phase(values,ch,hz):
        # Thirty/forty-five complete cycles avoid a finite-window phase bias.
        return math.atan2(-sum(values[n*2+ch]*math.sin(2*math.pi*hz*n/48000) for n in range(1024,2464)),sum(values[n*2+ch]*math.cos(2*math.pi*hz*n/48000) for n in range(1024,2464)))
    for ch,hz in [(0,1000),(1,1500)]:
        delta=tone_phase(half_samples,ch,hz)-tone_phase(floor_samples,ch,hz)-2*math.pi*hz*.5/48000
        check(abs(math.atan2(math.sin(delta),math.cos(delta)))<2e-4,'Fractional phase rounded or drifted')
    check(floor_receipt['renderKey']!=half_receipt['renderKey'] and floor_data!=half_data,'Fractional render reused whole-frame identity/audio')
    equivalent=dict(half,operation=str(uuid.uuid4()),firstFraction=2,firstDenominator=4)
    r,rr=run(equivalent);check(r.returncode==0 and rr[-1]['renderKey']==half_receipt['renderKey'] and rr[-1]['audioSha256']==half_receipt['audioSha256'],'Equivalent rational anchors differ')
    unsupported=fresh(protocol='sc-stretch-render-v1');r,rr=run(unsupported)
    check(r.returncode!=0 and not (jobs/unsupported['operation']).exists(),'Older origin protocol silently interpreted')
    for changes in [{'firstFraction':1,'firstDenominator':0},{'firstFraction':1,'firstDenominator':1},{'firstFraction':True},{'firstFraction':-1},{'firstDenominator':18446744073709551616},{'rate':384000},{'frames':12001},{'timeDenominator':0},{'timeNumerator':True},{'pitchMilliCents':18446744073707151616},{'sha256':'0'*64},{'relative':'../media/tone.wav'},{'channels':0},{'formantPreserved':1},{'pitchMilliCents':2400001},{'first':11999,'frames':2}]:
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
    # Valid geometry exceeds an OS-enforced child ceiling. A larger Debug/runtime
    # footprint may refuse watchdog thread creation before the opaque workspace.
    digest_wide=owned_wave(path,192000,32,16384);req=fresh(sha256=digest_wide,rate=192000,channels=32,sourceFrames=16384,frames=16384)
    r,rr=run(req,memory=32)
    error=json.loads(r.stderr)
    check(r.returncode!=0 and error.get('messageId')=='stretch.resource_limit' and
          not (jobs/req['operation']).exists(),'Worker initialization did not report resource refusal before mutation: '+json.dumps({'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr,'jobExists':(jobs/req['operation']).exists()}))
    # Same valid geometry succeeds with a larger ceiling. A span-validation
    # refusal therefore cannot satisfy the low-memory assertion above.
    control=dict(req,operation=str(uuid.uuid4()));r,rr=run(control,memory=256,deadline=60000)
    check(r.returncode==0 and rr[0].get('event')=='ready' and rr[-1]['writtenFrames']==24576 and
          rr[-1]['memoryCeilingBytes']==256*1024*1024 and (jobs/control['operation']/'complete.json').is_file(),
          'Memory-ceiling positive control did not complete the admitted source span')
    # Reviewed one-frame drain counterexample: conservative prepared-window
    # admission refuses BEFORE any operation/intent. Actual boundary renders
    # cover rates and both extreme pitch/time settings; no repaired output.
    boundary_jobs=0
    for rate,minimum in [(8000,2048),(48000,4096),(192000,16384)]:
        digest_boundary=owned_wave(path,rate,1,minimum+512)
        for time_n,time_d in [(1,4),(1,1),(4,1)]:
            for pitch in [-2400000,0,2400000]:
                for frames in [1,minimum-1]:
                    req=fresh(sha256=digest_boundary,rate=rate,channels=1,sourceFrames=minimum+512,frames=frames,timeNumerator=time_n,timeDenominator=time_d,pitchMilliCents=pitch)
                    r,rr=run(req)
                    if time_n==time_d and pitch==0:
                        check(r.returncode==0 and rr[-1]['writtenFrames']==frames and rr[-1]['processor']=='soundcurrent.stretch-positioned-copy-v1','Short unity span did not copy exactly')
                        _,_,copied=read_rf64(jobs/req['operation']/'audio.wav')
                        check(copied==path.read_bytes()[44:44+frames*4],'Unity render changed the raw samples')
                    else:
                        check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Short processed span reached destination mutation')
                req=fresh(sha256=digest_boundary,rate=rate,channels=1,sourceFrames=minimum+512,frames=minimum,timeNumerator=time_n,timeDenominator=time_d,pitchMilliCents=pitch)
                r,rr=run(req);target=(minimum*time_n+time_d-1)//time_d
                check(r.returncode==0 and rr[-1]['writtenFrames']==target,'Prepared window boundary did not render exact duration: '+r.stderr)
                boundary_jobs+=1
    check(boundary_jobs==27,'Boundary workflow matrix incomplete')
    # The copy route retains full-bandwidth source data at the DAW's 384k limit
    # and does not fold/reorder distinct discrete channels.
    copy_digest=owned_wave(path,384000,256,64)
    bits=bytearray(path.read_bytes())
    for ch,value in enumerate((0x80000000,0x00000001,0x80000001,0x3f800000,0xbf800000,0x40000000)):
        struct.pack_into('<I',bits,44+(17*256+ch)*4,value)
    path.write_bytes(bits);copy_digest=hashlib.sha256(bits).hexdigest()
    copy_request=fresh(sha256=copy_digest,rate=384000,channels=256,sourceFrames=64,
                       first=17,frames=1,timeNumerator=1,timeDenominator=1,pitchMilliCents=0)
    r,rr=run(copy_request);check(r.returncode==0 and rr[-1]['writtenFrames']==1,'384k/256-channel one-frame copy refused')
    cc,cr,cp=read_rf64(jobs/copy_request['operation']/'audio.wav')
    check(cc==256 and cr==384000 and cp==path.read_bytes()[44+17*256*4:44+18*256*4],
          '384k discrete unity copy changed bandwidth/channel order/sample bits')
    for change in [{'protocol':'sc-stretch-render-v2'}, {'protocol':'sc-stretch-render-v3'}, {'processor':'unknown'},
                   {'processor':'soundcurrent.stretch-rubberband4-r3-positioned-v2'}]:
        bad=dict(copy_request,operation=str(uuid.uuid4()),**change)
        r,rr=run(bad);check(r.returncode!=0 and not (jobs/bad['operation']).exists(),
                           'Legacy protocol or wrong unity algorithm reached destination mutation')
    # Exact copy remains exact when its one visible frame is surrounded by real
    # context, including the signed-zero/subnormal/headroom channels above.
    region_copy=fresh(sha256=copy_digest,rate=384000,channels=256,sourceFrames=64,first=17,frames=1,
                      timeNumerator=1,timeDenominator=1,pitchMilliCents=0,contextEnabled=True,contextBefore=2,contextAfter=3)
    r,rr=run(region_copy);check(r.returncode==0 and rr[-1]['writtenFrames']==6,'One-frame explicit-context copy refused')
    _,_,region_pcm=read_rf64(jobs/region_copy['operation']/'audio.wav')
    check(region_pcm[2*256*4:3*256*4]==cp,'Real context changed unity visible samples')
    region_jobs=1
    region_digest=owned_wave(path,48000,2,32768)
    pair=None
    for fraction,den in [(0,1),(1,2)]:
        for n,d,pitch in [(1,1,0),(3,2,0),(3,2,700007),(1,4,-2400000),(4,1,2400000)]:
            req=fresh(sha256=region_digest,rate=48000,channels=2,sourceFrames=32768,first=16320,
                      firstFraction=fraction,firstDenominator=den,frames=128,timeNumerator=n,timeDenominator=d,
                      pitchMilliCents=pitch,formantPreserved=True,contextEnabled=True,contextBefore=4095,contextAfter=4096)
            r,rr=run(req);check(r.returncode==0,'Explicit128-frame selected span did not render real context: '+r.stderr)
            complete=rr[-1];target=(8319*n+d-1)//d
            check(complete['frames']==128 and complete['target']==complete['writtenFrames']==target and
                  complete['contextBefore']==4095 and complete['contextAfter']==4096 and
                  complete['timeNumerator']==n and complete['timeDenominator']==d and
                  complete['cropMap']=='soundcurrent.stretch-region-nominal-v1','Region receipt lost visible/source/map identity')
            _,_,pcm=read_rf64(jobs/req['operation']/'audio.wav')
            check(len(pcm)==target*8 and all(math.isfinite(x) for x in struct.unpack('<'+'f'*(len(pcm)//4),pcm)),
                  'Region output exceeded exact duration or finite PCM')
            key={name:complete[name] for name in ('processor','sourceSha256','rate','channels','first','firstFraction','firstDenominator',
                  'sourceAlgorithm','frames','target','pitchMilliCents','formantPreserved','channelPolicy','contextBefore','contextAfter',
                  'timeNumerator','timeDenominator','cropMap')}
            check(hashlib.sha256(json.dumps(key,sort_keys=True,separators=(',',':')).encode()).hexdigest()==complete['renderKey'],
                  'Independent region-key binding failed')
            if fraction==0 and n==3 and d==2 and pitch==0:pair=(req,complete)
            region_jobs+=1
    equivalent=dict(pair[0],operation=str(uuid.uuid4()),timeNumerator=12479,timeDenominator=8319)
    r,rr=run(equivalent);check(r.returncode==0 and rr[-1]['target']==pair[1]['target'] and
                             rr[-1]['renderKey']!=pair[1]['renderKey'],'Different nominal crops with one rounded target reused a key')
    region_jobs+=1;check(region_jobs==12,'Explicit region workflow matrix incomplete')
    for change in [{'contextEnabled':False},{'contextEnabled':1},{'contextBefore':-1},{'contextBefore':16321},
                   {'contextAfter':32768},{'processor':'soundcurrent.stretch-rubberband4-r3-positioned-v2'},
                   {'contextBefore':1000000000,'contextAfter':1000000000}]:
        req=dict(pair[0],operation=str(uuid.uuid4()),**change)
        r,rr=run(req);check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Bad or unrequested context mutated a destination')
    req=dict(pair[0],operation=str(uuid.uuid4()));r,rr=run(req,maximum=8000)
    check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Visible-only frame grant admitted an oversized processing region')
    req=dict(pair[0],operation=str(uuid.uuid4()),contextBefore=0,contextAfter=0)
    r,rr=run(req);check(r.returncode!=0 and not (jobs/req['operation']).exists(),'Explicit short region bypassed prepared-window admission')
    print(json.dumps({'checks':checks,'actualOwnedCompletedJobs':14+boundary_jobs+region_jobs,'preparedBoundaryCompletedJobs':boundary_jobs,'unityShortCompletedJobs':7,'shortProcessedSpanRefusalBeforeMutation':True,'unityIntegerCopyExact':True,'unity384kDiscreteCopyExact':True,'explicitRegionCompletedJobs':region_jobs,'regionCopyExact':True,'sameRoundedTargetDistinctKeys':True,'realContextRefusalBeforeMutation':True,'headroom':True,'fractionalSourcePhase':True,'equivalentRationalAnchors':True,'exactCeilCounterexample':6422,'cooperativeCancel':True,'midRenderCancel':True,'hardDeadline':True,'OSMemoryRefusal':True,'memoryValidSpanPositiveControl':True,'nativePlatform':sys.platform,'nativeAudio':False}))
    if args.qualification:
        check(not git('status','--porcelain','--untracked-files=no') and git('rev-parse','HEAD')==source_commit,
              'Qualification source changed during acceptance')
        check(executable_hash(worker)==worker_hash and executable_hash(verifier)==verifier_hash,
              'Qualification executables changed during acceptance')
        verifier_checks=int(re.search(r'derived_shared_live_export_checks=(\d+)',artifact.stdout)[1])
        qualification={'format':'sc-stretch-worker-qualification-v3','sourceCommit':source_commit,
                       'sourceTree':source_tree,'nativePlatform':sys.platform,'exeSha256':worker_hash,
                       'verifierExeSha256':verifier_hash,'pid':result.pid,'exitCode':result.returncode,
                       'verifierPid':artifact_process.pid,'verifierExitCode':artifact.returncode,
                       'verifierChecks':verifier_checks,'checks':checks,'verifiedArtifact':True,
                       'unityShortCompletedJobs':7,'unityIntegerCopyExact':True,'unity384kDiscreteCopyExact':True,
                       'explicitRegionCompletedJobs':region_jobs,'regionCopyExact':True,'sameRoundedTargetDistinctKeys':True,'realContextRefusalBeforeMutation':True,
                       'nativeAudio':False,**{key:receipt[key] for key in
                        ('protocol','processor','operation','complete','writtenFrames','audioSha256','sampleSha256')}}
        args.qualification.parent.mkdir(parents=True,exist_ok=True)
        with args.qualification.open('x',encoding='utf-8') as f:f.write(json.dumps(qualification,indent=2)+'\n')
