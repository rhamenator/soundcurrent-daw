#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual native renders and independent full-PCM assembly inspection; no FFT/listening claim."""
from pathlib import Path
import argparse, array, datetime, hashlib, json, math, os, struct, subprocess, sys, time
os.environ["OPENBLAS_NUM_THREADS"]="1"
os.environ["OMP_NUM_THREADS"]="1"
def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS,(512*1024*1024,512*1024*1024))
p=argparse.ArgumentParser();p.add_argument('probe',type=Path);p.add_argument('output',type=Path);p.add_argument('--new-run-under',action='store_true');args=p.parse_args()
root=args.output.resolve()
if args.new_run_under:
    root.mkdir(parents=True,exist_ok=True)
    root=root/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
root.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;contract=json.loads((source/'contract.json').read_text());probe=args.probe.resolve()
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
records=[];checks=0;started=time.monotonic()
def check(value,reason):
    global checks
    checks+=1
    assert value,reason
def pcm(path,channels,frames):
    data=path.read_bytes();check(data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt ' and data[36:40]==b'data','Owned PCM header')
    check(struct.unpack_from('<HHIIHH',data,20)==(3,channels,48000,48000*channels*4,channels*4,32),'Float native layout')
    check(len(data)==44+frames*channels*4 and struct.unpack_from('<I',data,40)[0]==len(data)-44,'Exact full PCM duration')
    values=array.array('f');values.frombytes(data[44:])
    if sys.byteorder!='little':values.byteswap()
    check(all(math.isfinite(v) for v in values),'Finite full PCM')
    return data[44:]
def inspect(report,folder):
    j=report['request'];channels=2 if j['family']=='impulse' else 8;target=32768 if j['profile']=='identity' else 49152
    events=contract['bank']['events'];targets=events if j['profile']=='identity' else ([v*3//2 for v in events] if j['profile']=='uniform' else contract['bank']['nonuniformTargets'])
    check(report['frames']==32768 and report['target']==report['writtenFrames']==target and report['channels']==channels,'Frozen native bank')
    check(all(report[k] is False for k in ['nativeAudio','shippingAdopted','fullQualityQualified']),'Native scope')
    a=pcm(folder/'source.wav',channels,32768);b=pcm(folder/'rendered.wav',channels,target);size=channels*4
    points=[{'source':0,'output':0}];spans=[];gaps=[];previous=(0,0)
    for i,(event,out) in enumerate(zip(events,targets)):
        sb,se,ob,oe=event-256,event+2048,out-256,out+2048
        spans.append({'owner':f'00000000-0000-0000-0000-{i+1:012x}','sourceAnchor':event,'outputAnchor':out,'sourceBegin':sb,'sourceEnd':se,'outputBegin':ob,'outputEnd':oe,
                      'coreSourceBegin':sb+64,'coreSourceEnd':se-64,'coreOutputBegin':ob+64,'coreOutputEnd':oe-64})
        if sb!=previous[0]:gaps.append((previous[0],sb,previous[1],ob))
        points += [{'source':sb,'output':ob},{'source':event,'output':out},{'source':se,'output':oe}];previous=(se,oe)
    gaps.append((previous[0],32768,previous[1],target));points.append({'source':32768,'output':target})
    check(report['spans']==spans and report['points']==points,'Independent user IDs and derived map')
    check(len(report['gaps'])==len(gaps),'All admitted gaps')
    assembly=bytearray(target*size)
    for span in spans:
        sb,se,ob,oe=[span[k] for k in ['sourceBegin','sourceEnd','outputBegin','outputEnd']]
        assembly[ob*size:oe*size]=a[sb*size:se*size]
        check(a[(sb+64)*size:(se-64)*size]==b[(ob+64)*size:(oe-64)*size],'Every protected-core sample byte')
    for i,(sb,se,ob,oe) in enumerate(gaps):
        before=64 if sb else 0;after=64 if se<32768 else 0;count=se-sb+before+after;outCount=oe-ob+before+after
        expected={'index':i,'sourceBegin':sb,'sourceEnd':se,'outputBegin':ob,'outputEnd':oe,'contextBefore':before,'contextAfter':after,'inputFrames':count,'outputFrames':outCount,'unityCopy':se-sb==oe-ob}
        check(report['gaps'][i]==expected,'Actual gap/context/processor metadata')
        raw=pcm(folder/f'gap-{i}-source.wav',channels,count);generated=pcm(folder/f'gap-{i}-generated.wav',channels,outCount)
        check(raw==a[(sb-before)*size:(se+after)*size],'Only declared actual-source context')
        if expected['unityCopy']:check(raw==generated,'Unity gap exact bytes')
        assembly[ob*size:oe*size]=generated[before*size:(outCount-after)*size]
        for f in list(range(before))+list(range(outCount-after,outCount)):
            w=(f+1)/(before+1) if f<before else (outCount-f)/(after+1)
            for ch in range(channels):
                offset=((ob-before+f)*channels+ch)*4;sample=struct.unpack_from('<f',generated,(f*channels+ch)*4)[0];original=struct.unpack_from('<f',assembly,offset)[0]
                value=sample if original==sample else (1-w)*original+w*sample
                struct.pack_into('<f',assembly,offset,value)
    check(bytes(assembly)==b,'Independent complete halo/gap/core assembly')
    if j['profile']=='identity':check(a==b,'Identity all sample bytes')
    return {'exactCoreRegions':len(spans),'completeAssemblyExact':True,'fullQualityQualified':False}
for family in contract['bank']['families']:
    for profile in contract['bank']['profiles']:
        request={'family':family,'profile':profile};folder=root/f'case-{len(records):02d}';deadline=min(10,60-(time.monotonic()-started));check(deadline>0,'Bank deadline')
        childStart=time.monotonic();child=subprocess.Popen([str(probe),json.dumps(request),str(folder)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,preexec_fn=limits if sys.platform.startswith("linux") else None)
        timeout=False
        try:out,err=child.communicate(timeout=deadline)
        except subprocess.TimeoutExpired:timeout=True;child.kill();out,err=child.communicate()
        row={'id':len(records),'request':request,'pid':child.pid,'exitCode':child.returncode,'timeout':timeout,'seconds':time.monotonic()-childStart,'stdout':out,'stderr':err,'waves':{}}
        if folder.exists():row['waves']={path.name:{'sha256':sha(path),'bytes':path.stat().st_size} for path in sorted(folder.glob('*.wav'))}
        records.append(row);(root/'processes.json').write_text(json.dumps(records,indent=2)+'\n')
        check(child.returncode==0 and not timeout and err=='','Actual terminal renderer')
        report=json.loads(out);check(report==json.loads((folder/'render.json').read_text()) and report['request']==request,'Actual render report')
        row['inspection']=inspect(report,folder)
        (root/'processes.json').write_text(json.dumps(records,indent=2)+'\n')
summary={'format':'sc-protected-warp-live-oracle-v1','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'actualProcesses':len(records),'terminalSuccesses':sum(r['exitCode']==0 for r in records),
         'seconds':time.monotonic()-started,'independentChecks':checks,'probeSha256':sha(probe),'oracleSha256':sha(Path(__file__)),'contractSha256':sha(source/'contract.json'),
         'sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip(),'nativePlatform':sys.platform,'fullQualityQualified':False,'nativeAudio':False,
         'addressSpaceLimitApplied':sys.platform.startswith('linux'),'completeWaveReferences':sum(len(r['waves']) for r in records)}
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
