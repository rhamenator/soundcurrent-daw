#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Desktop creation and explicit adoption from real protected nominal jobs."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile
from worker_observation import observe,diagnostic
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','assembly_fixture','nominal_worker','controller']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
for name in ['worker','span_fixture','assembly_fixture','nominal_worker','controller']:setattr(a,name,getattr(a,name).resolve())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();binary=sha(a.worker);nominal_binary=sha(a.nominal_worker);controller_binary=sha(a.controller)
a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();rows=[];reports=[]
def driver(exe,mode,root,expected=0,**data):
    command=[str(exe)]+([mode] if mode else []);proc=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),workerSha256=binary,**data)),timeout=180 if exe==a.controller else 60)
    except BaseException:
        proc.kill();out,err=proc.communicate();(root/('interrupted-'+str(proc.pid)+'.stdout')).write_text(out);(root/('interrupted-'+str(proc.pid)+'.stderr')).write_text(err);raise
    prefix=f'{len(rows)}-{mode or "desktop"}';(root/(prefix+'.stdout')).write_text(out);(root/(prefix+'.stderr')).write_text(err)
    rows.append(dict(kind=mode or 'desktop',pid=proc.pid,exitCode=proc.returncode,expectedExitCode=expected,root=str(root),stdout=prefix+'.stdout',stderr=prefix+'.stderr'))
    assert proc.returncode==expected,(root,command,proc.returncode,err);return json.loads(out)
def run(root,body,side):
    def ready(line):
        packet=json.loads(line)
        if packet.get('event')=='ready':driver(a.assembly_fixture,'nominal-ready',root,fragment=side,ready=packet)
    cmd=[str(a.nominal_worker),'--project-root',str(root),'--jobs-root',str(root/'media/derived'),'--memory-mib','256','--maximum-input-frames','1000000000','--maximum-output-bytes',str(8*1024*1024*1024),'--deadline-ms','60000']
    result=observe(cmd,body,ready,timeout=30);prefix=f'{len(rows)}-worker';(root/(prefix+'.stdout')).write_text(result.stdout);(root/(prefix+'.stderr')).write_text(result.stderr)
    rows.append(dict(kind='nominal-worker',pid=result.pid,exitCode=result.returncode,seconds=result.elapsed_seconds,retirementPending=result.retirement_pending,observationTimedOut=result.observation_timed_out,outputLimited=result.output_limited,root=str(root),stdout=prefix+'.stdout',stderr=prefix+'.stderr'))
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
    originals={str(p):sha(p) for p in root.rglob('*') if p.is_file()}
    report=driver(a.controller,None,root,evidence=str(root.parent/'desktop'),protected=True,pairKind=kind)
    assert originals=={str(p):sha(Path(p)) for p in originals}
    cases=report['cases'];assert len(cases)==(8 if rate==48000 and kind=='protected' else 1)
    assert all(c['actualProjectCreditsReleased'] for c in cases)
    successful=cases[0]
    assert successful['actualTimelineDialog'] and successful['noAudioBeforeApply'] and successful['undoRedoSaveReopen']
    assert successful['pairFormat']=='sc-comp-pair-selection-protected-v1'
    assert successful['spanProtocols']==['sc-comp-span-protected-v1',{'protected':'sc-comp-span-protected-v1','uniform':'sc-comp-span-v1','existing':None}[kind]]
    assert len(successful['newChildren'])==(1 if kind=='existing' else 2)
    assert all(c['reaped'] and c['exitCode']==0 for c in successful['newChildren'])
    if len(cases)>1:
        assert cases[6]['fractionalGeometryRefusedBeforeSidecars'] and cases[7]['closeDuringPreparation']
        assert all(c['noChildLaunch'] for c in cases[1:])
    reports.append(dict(rate=rate,pairKind=kind,nominalProcessors=nominal_ids,originalInputsUnchanged=True,desktop=report))
assert sha(a.worker)==binary and sha(a.nominal_worker)==nominal_binary and sha(a.controller)==controller_binary
(scope/'report.json').write_text(json.dumps(dict(workerSha256=binary,nominalWorkerSha256=nominal_binary,controllerSha256=controller_binary,processes=rows,reports=reports,actualNominalWorkers=True,actualTimelineDialog=True,physicalAudio=False,qualityAdmission=False),indent=2)+'\n')
print(json.dumps(dict(scope=str(scope),pairs=len(reports),desktopCases=12,qualityAdmission=False)))
