#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser();p.add_argument('worker',type=Path);p.add_argument('span_fixture',type=Path);p.add_argument('pair_fixture',type=Path);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--protected',action='store_true');a=p.parse_args();worker=a.worker.resolve();binary=hashlib.sha256(worker.read_bytes()).hexdigest();a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[]
def driver(exe,mode,root,**data):
    proc=subprocess.Popen([str(exe.resolve()),mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);payload=json.dumps(dict(root=str(root),workerSha256=binary,**data))
    try:out,err=proc.communicate(payload,timeout=30)
    except BaseException:
        proc.kill();out,err=proc.communicate();(root/(mode+'.interrupted.stdout')).write_text(out);(root/(mode+'.interrupted.stderr')).write_text(err);raise
    prefix=f'{len(rows)}-{mode}';(root/(prefix+'.stdout')).write_text(out);(root/(prefix+'.stderr')).write_text(err);rows.append(dict(kind=mode,pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);return json.loads(out)
def run(root,request,action,deadline=10000):
    body=(root/request['file']).read_text();op=request['operation'];states=[];ready_packets=[]
    def ready(line):
        packet=json.loads(line)
        if packet.get('event')=='ready':
            ready_packets.append(packet)
            if action=='start':states.append(driver(a.pair_fixture,'start',root,ready=packet))
            elif action=='cancel':states.append(driver(a.pair_fixture,'cancel',root))
    command=[str(worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms',str(deadline),'--request-sha256',request['digest']]
    result=observe(command,body,ready,timeout=15);prefix=f'{len(rows)}-{op}';(root/(prefix+'.stdout')).write_text(result.stdout);(root/(prefix+'.stderr')).write_text(result.stderr);rows.append(dict(kind='worker',pid=result.pid,exitCode=result.returncode,operation=op,action=action,states=states,readyPackets=ready_packets,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited));assert not result.observation_timed_out and not result.retirement_pending and not result.output_limited,diagnostic(result);return result
cases=[(44100,'start'),(48000,'cancel'),(96000,'wait')]
if a.protected:cases += [(48000,'start'),(96000,'start')]
for rate,action in cases:
    root=scope/(f'case-{rate}-{action}' if a.protected else f'case-{rate}')/'project-é-日本';root.mkdir(parents=True);requests=driver(a.span_fixture,'prepare',root,rate=rate,pitch=0);driver(a.pair_fixture,'prepare',root,deadline=500 if action=='wait' else 10000,protected=a.protected)
    absent=driver(a.pair_fixture,'inspect',root);assert not absent['ready'] and all(c['state']==1 for c in absent['children'])
    if action=='start':
        for r in requests:assert run(root,r,'start').returncode==0
        driver(a.pair_fixture,'verify',root)
        driver(a.pair_fixture,'existing',root)
    else:
        result=run(root,requests[0],action,500 if action=='wait' else 10000);assert result.returncode!=0
        recovered=driver(a.pair_fixture,'inspect',root);assert not recovered['ready']
        # canceled after retirement, or interrupted waiting before start
        assert recovered['children'][0]['state']==(5 if action=='cancel' else 3)
        if action=='wait':
            packets=[json.loads(line) for line in result.stdout.splitlines() if json.loads(line).get('event')=='ready'];assert len(packets)==1
            interrupted=driver(a.pair_fixture,'retired',root,ready=packets[0]);assert interrupted['children'][0]['state']==4
(scope/'processes.json').write_text(json.dumps(dict(workerSha256=binary,processes=rows,pairedCandidatesRecovered=True,protectedPolicy=a.protected,modelAdoptionChanged=False,qualityAdmission=False),indent=2)+'\n');print(json.dumps(dict(scope=str(scope),workers=sum(r['kind']=='worker' for r in rows),qualityAdmission=False)))
