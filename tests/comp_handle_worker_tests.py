#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual existing worker renders, dual artifact review and independent PCM checks."""
from pathlib import Path
import argparse,hashlib,json,math,struct,subprocess,tempfile,uuid
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser();p.add_argument('worker',type=Path);p.add_argument('fixture',type=Path);p.add_argument('--evidence',type=Path);args=p.parse_args()
worker=args.worker.resolve();fixture=args.fixture.resolve()
if args.evidence:
    args.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=args.evidence)) if args.evidence else Path(tempfile.mkdtemp(prefix='sc-comp-handles-'))
rows=[]
def driver(mode,root):
    process=subprocess.Popen([str(fixture),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    out,err=process.communicate(json.dumps({'root':str(root)}),timeout=30)
    (root/(mode+'.log')).write_text(out+err);assert process.returncode==0,(mode,out,err)
    rows.append({'mode':mode,'pid':process.pid,'exit_code':process.returncode,'root':str(root)})
    return json.loads(out)
def run_worker(root,request):
    jobs=root/'media/derived';jobs.mkdir(exist_ok=True);op=request['operation']
    def ready(line):
        packet=json.loads(line)
        if packet.get('event')=='ready':
            assert packet['operation']==op
            (jobs/op/'start.request').write_text('start\n')
    command=[str(worker),'--project-root',str(root),'--jobs-root',str(jobs),'--memory-mib','64','--maximum-input-frames','1000000','--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000']
    result=observe(command,json.dumps(request),ready,timeout=15)
    (root/(op+'.worker.log')).write_text(result.stdout+result.stderr)
    assert not result.observation_timed_out and not result.output_limited and not result.retirement_pending and result.returncode==0,diagnostic(result)
    packets=[json.loads(line) for line in result.stdout.splitlines()]
    rows.append({'mode':'worker','root':str(root),'operation':op,'pid':result.pid,'exit_code':result.returncode})
    return next(j for j in packets if j.get('event')=='ready')
for channels,fraction in [(1,0),(2,0),(2,1),(4,0)]:
    root=(scope/('mono' if channels==1 else f'ch{channels}-fraction{fraction}')/'project-é-日本').resolve();(root/'media').mkdir(parents=True)
    rate=48000;frames=4096
    source=[float(2.5*math.sin((f+ch*13)*.017)+.2*math.cos(f*.059)) for f in range(frames) for ch in range(channels)]
    payload=b''.join(struct.pack('<f',v) for v in source);source=list(struct.unpack('<'+'f'*len(source),payload))
    fmt=struct.pack('<HHIIHH',3,channels,rate,rate*channels*4,channels*4,32)
    wav=b'RIFF'+struct.pack('<I',36+len(payload))+b'WAVEfmt '+struct.pack('<I',16)+fmt+b'data'+struct.pack('<I',len(payload))+payload
    path=root/'media/raw.wav';path.write_bytes(wav);raw_hash=hashlib.sha256(wav).hexdigest();asset=str(uuid.uuid4())
    requests=[dict(protocol='sc-stretch-render-v4',processor='soundcurrent.stretch-positioned-copy-v1',operation=str(uuid.uuid4()),assetId=asset,relative='media/raw.wav',sha256=raw_hash,rate=rate,channels=channels,sourceFrames=frames,first=512+n*384,firstFraction=fraction,firstDenominator=7 if fraction else 1,frames=384,timeNumerator=1,timeDenominator=1,pitchMilliCents=0,formantPreserved=False,contextEnabled=False,contextBefore=0,contextAfter=0) for n in range(2)]
    (root/'base-requests.json').write_text(json.dumps(requests));ready=[run_worker(root,r) for r in requests];(root/'base-ready.json').write_text(json.dumps(ready))
    generated=driver('prepare',root);ready=[run_worker(root,r) for r in generated];(root/'comp-ready.json').write_text(json.dumps(ready));report=driver('admit',root)
    assert path.read_bytes()==wav
    if not fraction:
        for curve in range(3):
            actual=struct.unpack('<'+'f'*(768*channels),(root/'exports'/f'comp-{curve}.wav.pcm').read_bytes())
            for f in range(768):
                project=f+100;gain=1
                if 458<=project<510:
                    x=(project-458)/51
                    gain=math.sin(x*math.pi/2)+math.cos(x*math.pi/2) if curve==1 else 1
                for ch in range(channels):
                    assert abs(actual[f*channels+ch]-source[(512+f)*channels+ch]*gain)<2e-6
    rows.append({'mode':'independent-pcm','root':str(root),'channels':channels,'fraction':fraction,'full_pcm_oracle':not fraction,'report':report})
(scope/'summary.json').write_text(json.dumps({'actual_worker_renders':16,'scenarios':4,'rows':rows,'full_sonic_quality':False,'physical_endpoints_used':False},indent=2)+'\n')
print('actual_comp_handle_renders=16 scenarios=4 atomic_admission_undo_reopen_live_export=true full_sonic_quality=false')
