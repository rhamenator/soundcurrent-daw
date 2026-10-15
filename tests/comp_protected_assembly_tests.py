#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real protected nominal/span workers, paired assembly, adoption and abrupt exit."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture']:setattr(a,name,getattr(a,name).resolve())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();binary=sha(a.worker);nominal_binary=sha(a.nominal_worker);job_binary=sha(a.job_fixture)
a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[]
def driver(exe,mode,root,expected=0,**data):
    command=[str(exe)]+([mode] if mode else []);proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**data)),timeout=180 if exe==a.job_fixture else 60)
    except BaseException:
        proc.kill();out,err=proc.communicate();(root/('interrupted-'+str(proc.pid)+'.stdout')).write_text(out);(root/('interrupted-'+str(proc.pid)+'.stderr')).write_text(err);raise
    prefix=f'{len(rows)}-{mode or "publication"}';(root/(prefix+'.stdout')).write_text(out);(root/(prefix+'.stderr')).write_text(err)
    rows.append(dict(kind=mode or 'publication',pid=proc.pid,exitCode=proc.returncode,expectedExitCode=expected,root=str(root),stdout=prefix+'.stdout',stderr=prefix+'.stderr'))
    assert proc.returncode==expected,(root,command,proc.returncode,err);return json.loads(out)
def run(root,body,request=None,side=0):
    nominal=request is None
    def ready(line):
        packet=json.loads(line)
        if packet.get('event')=='ready':
            if nominal:driver(a.assembly_fixture,'nominal-ready',root,fragment=side,ready=packet)
            else:driver(a.pair_fixture,'start',root,ready=packet)
    if nominal:cmd=[str(a.nominal_worker),'--project-root',str(root),'--jobs-root',str(root/'media/derived'),'--memory-mib','256','--maximum-input-frames','1000000000','--maximum-output-bytes',str(8*1024*1024*1024),'--deadline-ms','60000']
    else:cmd=[str(a.worker),'--project-root',str(root),'--jobs-root',str(root/'media/comp-spans'),'--memory-mib','256','--maximum-source-bytes',str(64*1024*1024),'--maximum-output-bytes',str(64*1024*1024),'--deadline-ms','10000','--request-sha256',request['digest']]
    result=observe(cmd,body,ready,timeout=30);prefix=f'{len(rows)}-worker';(root/(prefix+'.stdout')).write_text(result.stdout);(root/(prefix+'.stderr')).write_text(result.stderr)
    rows.append(dict(kind='nominal-worker' if nominal else 'span-worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited,root=str(root),stdout=prefix+'.stdout',stderr=prefix+'.stderr'))
    assert result.returncode==0 and not result.retirement_pending and not result.observation_timed_out and not result.output_limited,diagnostic(result)
for rate,kind in [(44100,'protected'),(48000,'protected'),(96000,'protected'),(48000,'uniform'),(48000,'existing')]:
    root=scope/f'case-{rate}-{kind}'/'project-é-日本';root.mkdir(parents=True)
    driver(a.span_fixture,'prepare',root,rate=rate)
    requests=driver(a.assembly_fixture,'nominal-prepare',root,pairKind=kind);(root/'media/derived').mkdir(exist_ok=True)
    nominal_ids=[r['processor'] if r else None for r in requests]
    assert nominal_ids==['soundcurrent.stretch-transient-protected-rubberband4-r3-v1',{'protected':'soundcurrent.stretch-transient-protected-rubberband4-r3-v1','uniform':'soundcurrent.stretch-rubberband4-r3-positioned-v2','existing':None}[kind]]
    for side,r in enumerate(requests):
        if r is not None:run(root,json.dumps(r),side=side)
    driver(a.assembly_fixture,'nominal-adopt',root)
    spans=driver(a.assembly_fixture,'prepare',root,physical=True,protected=True)
    protocols=[json.loads((root/r['file']).read_text())['protocol'] if r else None for r in spans]
    assert protocols==['sc-comp-span-protected-v1',{'protected':'sc-comp-span-protected-v1','uniform':'sc-comp-span-v1','existing':None}[kind]]
    driver(a.pair_fixture,'prepare',root,protected=True);driver(a.assembly_fixture,'unready',root)
    needed=[r for r in spans if r is not None]
    for i,r in enumerate(needed):
        run(root,(root/r['file']).read_text(),r)
        if i+1<len(needed):driver(a.assembly_fixture,'unready',root)
    assembly=driver(a.assembly_fixture,'verify',root);assert len(assembly['sides'])==(1 if kind=='existing' else 2)
    assert all(s['domain']==1 and s['peakLinear']>1 for s in assembly['sides'])
    publication=driver(a.job_fixture,None,root);adoption=publication['adoption']
    assert publication['executorSha256']==job_binary and publication['modelAdoptionChanged']
    assert all(adoption[k] for k in ['savedReopened','undoRedo','liveEqualsExport','registeredProjectRestored'])
    originals=[root/'media/raw.wav',root/'media/current.wav']+[p for d in ['derived','comp-spans'] for p in (root/'media'/d).rglob('*') if p.is_file()]
    before={str(p):sha(p) for p in originals};crashes=[]
    for stage in range(3):
        abrupt=driver(a.job_fixture,'--crash-publish',root,expected=86,stage=stage)
        recovered=driver(a.job_fixture,'--inspect-crash',root,operation=abrupt['operation'],stage=stage)
        assert abrupt['abruptExit'] and abrupt['stage']==stage and abrupt['operation']==recovered['operation']
        assert recovered['interrupted'] and recovered['completionAbsent'] and recovered['resumeRefused'];crashes.append(dict(abrupt=abrupt,recovered=recovered))
    assert before=={str(p):sha(p) for p in originals}
    reports.append(dict(rate=rate,pairKind=kind,nominalProcessors=nominal_ids,spanProtocols=protocols,assembly=assembly,publication=publication,crashes=crashes,originalInputsUnchanged=True))
assert sha(a.worker)==binary and sha(a.nominal_worker)==nominal_binary and sha(a.job_fixture)==job_binary
(scope/'report.json').write_text(json.dumps(dict(workerSha256=binary,nominalWorkerSha256=nominal_binary,publicationExecutorSha256=job_binary,processes=rows,reports=reports,actualNominalWorkers=True,abruptProcessExits=15,actualPowerLoss=False,desktopEntryEnabled=False,physicalAudio=False,qualityAdmission=False),indent=2)+'\n')
print(json.dumps(dict(scope=str(scope),pairs=len(reports),abruptProcessExits=15,qualityAdmission=False)))
