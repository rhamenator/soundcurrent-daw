#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,uuid,copy
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser();p.add_argument('worker',type=Path);p.add_argument('fixture',type=Path);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();worker=a.worker.resolve();fixture=a.fixture.resolve();sha=lambda b:hashlib.sha256(b).hexdigest();binary=sha(worker.read_bytes())
a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[]
def driver(mode,root,**extra):
 command=[str(fixture),mode];proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**extra)),timeout=30)
 except BaseException:
  proc.kill();out,err=proc.communicate();(root/(mode+'.interrupted.stdout')).write_text(out);(root/(mode+'.interrupted.stderr')).write_text(err);raise
 (root/(mode+'.stdout')).write_text(out);(root/(mode+'.stderr')).write_text(err);rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);return json.loads(out)
def run(root,body,expected,mode='start',deadline=10000):
 op=json.loads(body)['operation'];packets=[]
 def packet(line):
  j=json.loads(line);packets.append(j)
  if j.get('event')=='ready':
   if mode=='start' and root.parent.name=='case-44100':driver('active',root,fragment=j['fragment'])
   (root/(op+'.ready.json')).write_text(json.dumps(j))
   if mode=='start':(root/(f'ready-{j["fragment"]}.json')).write_text(json.dumps(j))
   if mode=='changed-after-ready':
    raw=root/'media/raw.wav';data=bytearray(raw.read_bytes());data[-1]^=1;raw.write_bytes(data)
   if mode in ['start','cancel','changed-after-ready']:(root/'media/comp-spans'/op/('cancel.request' if mode=='cancel' else 'start.request')).write_text('ack\n')
 command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms',str(deadline),'--request-sha256',expected]
 prefix=str(len(rows))+'-'+op;result=observe(command,body,packet,timeout=15);(root/(prefix+'.worker.stdout')).write_text(result.stdout);(root/(prefix+'.worker.stderr')).write_text(result.stderr)
 rows.append(dict(kind='worker',pid=result.pid,exitCode=result.returncode,operation=op,mode=mode,deadline=deadline,packets=packets,stdoutFile=prefix+'.worker.stdout',stderrFile=prefix+'.worker.stderr'))
 assert not result.observation_timed_out and not result.retirement_pending and not result.output_limited,diagnostic(result)
 return result,root/'media/comp-spans'/op
reports=[]
for rate in [44100,48000,96000]:
 root=scope/f'case-{rate}'/'project-é-日本';root.mkdir(parents=True);requests=driver('prepare',root,rate=rate)
 before={n:sha((root/'media'/n).read_bytes()) for n in ['raw.wav','current.wav']}
 for r in requests:
  body=(root/r['file']).read_text();result,job=run(root,body,r['digest']);assert result.returncode==0 and (job/'complete.json').exists(),diagnostic(result)
 reports.append(driver('verify',root))
 original_audio=root/'media/comp-spans'/requests[0]['operation']/'audio.wav';original_marker=original_audio.parent/'complete.json';saved_audio=original_audio.read_bytes();saved_marker=original_marker.read_bytes()
 body=(root/requests[0]['file']).read_text();result,_=run(root,body,requests[0]['digest']);assert result.returncode!=0 and original_audio.read_bytes()==saved_audio and original_marker.read_bytes()==saved_marker
 assert before=={n:sha((root/'media'/n).read_bytes()) for n in before}
 # Fresh child operation avoids reusing or rewriting completed jobs.
 original=json.loads((root/requests[0]['file']).read_text())
 for mode in ['cancel','wait']:
  req={**original,'operation':str(uuid.uuid4())};body=json.dumps(req);result,job=run(root,body,sha(body.encode()),mode,500 if mode=='wait' else 10000)
  assert result.returncode!=0 and not (job/'complete.json').exists() and (job/'intent.json').exists()
 for field,value in [('fragment',2),('fragment',0.0),('executor',{'id':'wrong','buildSha256':binary}),('executor',{'id':original['executor']['id'],'buildSha256':'0'*64})]:
  req={**original,'operation':str(uuid.uuid4()),field:value};body=json.dumps(req);result,job=run(root,body,sha(body.encode()));assert result.returncode!=0 and not job.exists()
 req={**original,'operation':str(uuid.uuid4())};body=json.dumps(req);result,job=run(root,body,'0'*64);assert result.returncode!=0 and not job.exists()
 # Canonical protected binding rejects edited fields even when the trusted test
 # parent supplies a new hash for those request bytes.
 for field,value in [('geometrySha256','0'*64),('rawOrigin',[0,0,1]),('rawFrames',1),('outputFrames',1),('inputRate',8000),('outputRate',8000),('semanticAnchors',0),('chunkFrames',256),('chunkFrames',512.0)]:
  req=copy.deepcopy(original);req['operation']=str(uuid.uuid4());req['protected'][field]=value;body=json.dumps(req)
  result,job=run(root,body,sha(body.encode()));assert result.returncode!=0 and not job.exists()
 req=copy.deepcopy(original);req['operation']=str(uuid.uuid4());req['protocol']='sc-comp-span-v1';req['executor']['id']='soundcurrent.comp-span-positioned-rubberband4-r3-v1';del req['protected'];body=json.dumps(req)
 result,job=run(root,body,sha(body.encode()));assert result.returncode!=0 and not job.exists()
 req=copy.deepcopy(original);req['operation']=str(uuid.uuid4());body=json.dumps(req).replace('"chunkFrames": 512','"chunkFrames": 512, "chunkFrames": 512')
 result,job=run(root,body,sha(body.encode()));assert result.returncode!=0 and not job.exists()
 saved=(root/'media/raw.wav').read_bytes();req={**original,'operation':str(uuid.uuid4())};body=json.dumps(req)
 try:
  result,job=run(root,body,sha(body.encode()),'changed-after-ready');assert result.returncode!=0 and (job/'intent.json').exists() and not (job/'complete.json').exists()
 finally:(root/'media/raw.wav').write_bytes(saved)
 # Input tampering is refused before any output operation is created.
 raw=root/'media/raw.wav';saved=raw.read_bytes();bad=bytearray(saved);bad[-1]^=1;raw.write_bytes(bad)
 req={**original,'operation':str(uuid.uuid4())};body=json.dumps(req);result,job=run(root,body,sha(body.encode()));assert result.returncode!=0 and not job.exists();raw.write_bytes(saved)
assert sha(worker.read_bytes())==binary
(scope/'processes.json').write_text(json.dumps(dict(workerSha256=binary,processes=rows,successfulSpanRenders=6,qualityAdmission=False,nonuniformProducerImplemented=True,workerProtocolIntegrated=True,protectedSamples=sum(r['protectedSamples'] for r in reports),completePcmSamples=sum(r['comparedSamples'] for r in reports),reports=reports,modelAdoptionChanged=False),indent=2)+'\n');print(json.dumps(dict(scope=str(scope),workers=sum(r['kind']=='worker' for r in rows),successfulSpans=6,qualityAdmission=False)))
