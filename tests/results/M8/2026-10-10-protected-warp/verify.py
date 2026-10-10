#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded retained source/process/map/full PCM inspection; no DSP or FFT replay."""
from pathlib import Path, PurePosixPath
from fractions import Fraction as F
import array, hashlib, json, math, struct, sys, zipfile
root=Path(__file__).resolve().parent;repo=root.parents[3];manifest=json.loads((root/'manifest.json').read_text());checks=0
sha=lambda data:hashlib.sha256(data).hexdigest()
def check(value,reason):
    global checks
    checks+=1
    if not value:raise AssertionError(reason)
archive=root/'capture.zip';check(manifest['format']=='sc-protected-warp-capture-v1','Format')
check(archive.stat().st_size==manifest['bytes']<64*1024*1024 and sha(archive.read_bytes())==manifest['sha256'],'Archive admission/hash')
for key in ['nativeWindowsReplayed','nativeAudio','fullQualityQualified','shippingAdopted','productBinaryUploaded']:check(manifest[key] is False,'Recorded scope')
with zipfile.ZipFile(archive) as z:
    check(set(z.namelist())==set(manifest['entries']) and len(z.namelist())==len(set(z.namelist())),'Exact unique member set')
    check(sum(v.file_size for v in z.infolist())==manifest['uncompressedBytes']<128*1024*1024,'Payload admission')
    for info in z.infolist():
        path=PurePosixPath(info.filename);check(not path.is_absolute() and '..' not in path.parts and '\\' not in info.filename and info.file_size<5*1024*1024,'Member admission')
        data=z.read(info);check(len(data)==manifest['entries'][info.filename]['bytes'] and sha(data)==manifest['entries'][info.filename]['sha256'],'Complete member identity')
    get=lambda name:json.loads(z.read(name));contract=get('experiment/contract.json');analysis=get('run-final/analysis.json')
    check(contract['sourceBase']==manifest['sourceBase']=='a8fa5f143e8b5d44b06ad6fe2be10f7b62c7360f','Production base')
    check(contract['policy']=={'beforeFrames':256,'afterFrames':2048,'haloFrames':64,'minimumNonunityGapFrames':64},'Frozen protection policy')
    expectedRequests=[{'family':family,'profile':profile} for family in contract['bank']['families'] for profile in contract['bank']['profiles']]
    banks=[]
    for label in ['initial','final']:
        prefix='run-'+label;summary=get(prefix+'/summary.json');records=get(prefix+'/processes.json');banks.append(records)
        receipt=get('build-'+label+'/receipt.json');preflight=get('build-'+label+'/preflight.json')
        check(len(records)==summary['actualProcesses']==summary['terminalSuccesses']==18 and summary['terminalFailures']==summary['notStarted']==0 and summary['seconds']<60,'Complete actual terminal bank')
        check(summary['limits']==contract['limits'] and summary['sourceWasUncommitted'] and summary['sourceBase']==summary['sourceCommit']==contract['sourceBase'],'Actual source/budget scope')
        check(contract['frozenAtUtc']<preflight['utc']<summary['utc'],'Preregistered before build/render')
        check(receipt['preflight']==preflight and receipt['after']==preflight['files'] and receipt['unchangedInputs'] and not receipt['timeout'] and receipt['exitCode']==0 and receipt['pid']>0 and receipt['seconds']<60,'Actual build identities/outcome')
        check(sha(z.read('build-'+label+'/preflight.json'))==receipt['preflightSha256'] and sha(z.read('build-'+label+'/receipt.json'))==summary['buildReceiptSha256'] and receipt['probeSha256']==summary['probeSha256'],'Executed binary/build binding')
        check(z.read('build-'+label+'/receipt.json')==z.read(prefix+'/build-receipt.json'),'Executed retained receipt')
        for name,digest in summary['sourceHashesBeforeRun'].items():
            check(sha(z.read(prefix+'/'+name))==digest,'Preregistered executed source')
            matches=[value for path,value in preflight['files'].items() if path.endswith('/experiments/protected-warp/'+name)]
            check(matches==[digest],'Build-time source identity')
        for name in ['warp_map.cpp','warp_map.hpp']:
            matches=[value for path,value in preflight['files'].items() if path.endswith('/experiments/warp-map/'+name)]
            check(matches==[sha(z.read('planner/'+name))],'Exact base planner input')
        check([r['request'] for r in records]==expectedRequests and [r['id'] for r in records]==list(range(18)),'Complete frozen request matrix')
        for r in records:
            check(r['pid']>0 and r['exitCode']==0 and not r['timeout'] and r['stderr']=='' and r['seconds']<10,'Terminal child')
            check(r['result']==json.loads(r['stdout'])==get(f"{prefix}/case-{r['id']:02d}/render.json") and r['result']['request']==r['request'],'Retained actual report')
            check(len(r['waves'])==12,'Full source/output and five source/generated gaps')
            for name,record in r['waves'].items():
                data=z.read('waves/'+record['sha256']+'.wav');check(len(data)==record['bytes'] and sha(data)==record['sha256'],'Every complete wave reference')
    finalPre=get('build-final/preflight.json')
    for name in [n[len('experiment/'):] for n in z.namelist() if n.startswith('experiment/')]:
        matches=[value for path,value in finalPre['files'].items() if path.endswith('/experiments/protected-warp/'+name)]
        check(matches==[sha(z.read('experiment/'+name))],'Final supplied experiment source binding')
        check(z.read('experiment/'+name)==(repo/'experiments/protected-warp'/name).read_bytes(),'Current committed experimental source')
    dependency=get('dependency-manifest.json')['rubberband'];check(dependency['version']=='4.0.0' and dependency['license']=='GPL-2.0-or-later' and dependency['patches']==[],'Existing licensed vendor pin')
    for name,record in dependency['files'].items():
        path=repo/'third_party/rubberband'/name;check(path.stat().st_size==record['bytes'] and sha(path.read_bytes())==record['sha256'],'Unchanged compiled vendor source/license')
    for original,current in zip(*banks):check(original['waves']==current['waves'] and original['result']==current['result'],'Complete initial/final PCM/report equality')
    check(get('run-initial/analysis.json')==analysis,'Exact repeated numerical observations')
    live=get('live-render/summary.json');liveRecords=get('live-render/processes.json')
    check(live['actualProcesses']==live['terminalSuccesses']==len(liveRecords)==18 and live['seconds']<60 and live['independentChecks']==1296 and live['addressSpaceLimitApplied'],'Actual CMake full PCM oracle')
    check(sha(z.read('experiment/live_oracle.py'))==live['oracleSha256'] and sha(z.read('experiment/contract.json'))==live['contractSha256'],'Live oracle source binding')
    for a,b in zip(banks[1],liveRecords):check(a['request']==b['request'] and a['waves']==b['waves'] and b['pid']>0 and b['exitCode']==0 and b['stderr']=='' and not b['timeout'],'Actual CMake PCM repeat')
    repeat=get('repeat-inspection.json');check(repeat['actualLocalProcesses']==manifest['actualLocalProcesses']==54 and repeat['completeWaveReferences']==manifest['completeWaveReferences']==648,'Recorded process/wave scope')
    check(analysis['analysisSourceSha256']==sha(z.read('experiment/analyze_bank.py')) and analysis['contractSha256']==sha(z.read('experiment/contract.json')),'Analysis source/contract identity')
    def pcm(record,name,channels,frames):
        ref=record['waves'][name];data=z.read('waves/'+ref['sha256']+'.wav')
        check(data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt ' and data[36:40]==b'data' and struct.unpack_from('<I',data,4)[0]==len(data)-8,'Owned WAV header')
        check(struct.unpack_from('<HHIIHH',data,20)==(3,channels,48000,48000*channels*4,channels*4,32),'Exact float channel layout')
        check(len(data)==44+frames*channels*4 and struct.unpack_from('<I',data,40)[0]==len(data)-44,'Full PCM duration')
        values=array.array('f');values.frombytes(data[44:])
        if sys.byteorder!='little':values.byteswap()
        check(all(math.isfinite(v) for v in values),'Finite full PCM')
        return data[44:]
    for r in banks[1]:
        report=r['result'];j=r['request'];channels=2 if j['family']=='impulse' else 8;target=32768 if j['profile']=='identity' else 49152;size=4*channels
        check(report['target']==report['writtenFrames']==target and report['frames']==32768 and report['channels']==channels,'Frozen frame/channel bank')
        events=contract['bank']['events'];targets=events if j['profile']=='identity' else ([v*3//2 for v in events] if j['profile']=='uniform' else contract['bank']['nonuniformTargets'])
        spans=[];points=[{'source':0,'output':0}];gaps=[];last=(0,0)
        for i,(event,out) in enumerate(zip(events,targets)):
            sb,se,ob,oe=event-256,event+2048,out-256,out+2048
            spans.append({'owner':f'00000000-0000-0000-0000-{i+1:012x}','sourceAnchor':event,'outputAnchor':out,'sourceBegin':sb,'sourceEnd':se,'outputBegin':ob,'outputEnd':oe,'coreSourceBegin':sb+64,'coreSourceEnd':se-64,'coreOutputBegin':ob+64,'coreOutputEnd':oe-64})
            gaps.append((last[0],sb,last[1],ob));points += [{'source':sb,'output':ob},{'source':event,'output':out},{'source':se,'output':oe}];last=(se,oe)
        gaps.append((last[0],32768,last[1],target));points.append({'source':32768,'output':target})
        check(report['spans']==spans and report['points']==points and len(report['gaps'])==5,'Independent owner/derived point geometry')
        a=pcm(r,'source.wav',channels,32768);b=pcm(r,'rendered.wav',channels,target);assembled=bytearray(target*size)
        for s in spans:
            sb,se,ob,oe=[s[k] for k in ['sourceBegin','sourceEnd','outputBegin','outputEnd']];assembled[ob*size:oe*size]=a[sb*size:se*size]
            check(a[(sb+64)*size:(se-64)*size]==b[(ob+64)*size:(oe-64)*size],'All copied core PCM')
        for i,(sb,se,ob,oe) in enumerate(gaps):
            before=64 if sb else 0;after=64 if se<32768 else 0;count=se-sb+before+after;outCount=oe-ob+before+after
            expected={'index':i,'sourceBegin':sb,'sourceEnd':se,'outputBegin':ob,'outputEnd':oe,'contextBefore':before,'contextAfter':after,'inputFrames':count,'outputFrames':outCount,'unityCopy':se-sb==oe-ob}
            check(report['gaps'][i]==expected,'Explicit real-source halo/gap duration')
            raw=pcm(r,f'gap-{i}-source.wav',channels,count);generated=pcm(r,f'gap-{i}-generated.wav',channels,outCount)
            check(raw==a[(sb-before)*size:(se+after)*size],'Actual declared source context')
            if expected['unityCopy']:check(raw==generated,'Unity gap exact bytes')
            assembled[ob*size:oe*size]=generated[before*size:(outCount-after)*size]
            for f in list(range(before))+list(range(outCount-after,outCount)):
                w=(f+1)/(before+1) if f<before else (outCount-f)/(after+1)
                for ch in range(channels):
                    offset=((ob-before+f)*channels+ch)*4;sample=struct.unpack_from('<f',generated,(f*channels+ch)*4)[0];previous=struct.unpack_from('<f',assembled,offset)[0]
                    struct.pack_into('<f',assembled,offset,sample if previous==sample else (1-w)*previous+w*sample)
            # Whole gap-derived byte strings remain retained, never synthesized by this verifier.
        check(bytes(assembled)==b,'Independent complete assembly of retained PCM')
        if j['profile']=='identity':check(a==b,'Full identity source/output')
        if j['family']=='cancellation':
            values=array.array('f');values.frombytes(b)
            if sys.byteorder!='little':values.byteswap()
            for ch in range(0,channels,2):check(max(abs(values[f+ch]+values[f+ch+1]) for f in range(0,len(values),channels))==0,'All output coincidence pairs exact')
    onsets=[o for c in analysis['cases'] for o in c['onsets']];phases=[o for c in analysis['cases'] for o in c['phase']];cancellation=[o for c in analysis['cases'] for o in c['cancellation']]
    check(len(onsets)==analysis['onsetObservations']==408 and sum(o['resolved'] for o in onsets)==analysis['resolvedOnsets']==328 and analysis['unresolvedOnsets']==80,'Resolved/censored event scope')
    check(sum(o['timingDiagnosticPassed'] for o in onsets)==analysis['timingDiagnosticPassed']==320,'Retained timing decisions')
    def mapping(points,x):
        for a,b in zip(points,points[1:]):
            if a['source']<=x<b['source']:return F(a['output'])+(x-a['source'])*F(b['output']-a['output'],b['source']-a['source'])
        raise AssertionError('Independent timing outside map')
    for o in onsets:
        if o['resolved']:
            expected=mapping(banks[1][o['case']]['result']['points'],F(o['event']+o['sourceBiasFrames']))+o['expectedPhysicalDelayFrames'];error=F(o['outputDetector']['frame'])-expected
            check(o['expectedDetectorFrame']==[expected.numerator,expected.denominator] and o['errorFrames']==float(error) and o['timingDiagnosticPassed']==(abs(error)<48),'Exact calibrated event timing')
        else:check(not o['timingDiagnosticPassed'],'Ambiguity is not a pass')
    check(sum(o['scope']=='core' for o in phases)==analysis['corePhaseObservations']==168 and sum(o['scope']=='core' and o['diagnosticPassed'] for o in phases)==analysis['corePhasePassed']==168,'Copied-core phase scope')
    check(sum(o['scope']=='gap' for o in phases)==analysis['gapPhaseObservations']==210 and sum(o['scope']=='gap' and o['diagnosticPassed'] for o in phases)==analysis['gapPhasePassed']==70,'Processed-gap phase scope')
    check(len(cancellation)==analysis['cancellationObservations']==analysis['cancellationDiagnosticPassed']==120 and all(o['maximumPairSum']==0 for o in cancellation),'Exact synthetic cancellation observations')
    check(analysis['silentPreechoObservations']==analysis['silentPreechoPassed']==216 and analysis['backgroundObservations']==192,'Silent/background interpretation remains separate')
    for key in ['fullQualityQualified','QStretchPassed','groupPhaseQualified','nativeAudio']:check(analysis[key] is False,'No quality promotion')
    geometry=get('live-geometry/summary.json');requests=get('live-geometry/requests.json');results=[json.loads(v) for v in z.read('live-geometry/stdout.jsonl').splitlines()]
    check(geometry['requests']==len(requests)==len(results)==35 and geometry['accepted']==11 and geometry['refused']==24 and geometry['independentChecks']==470 and geometry['exitCode']==0,'Actual admission/retirement bank')
    for c,r in zip(requests,results):check(r['accepted']==c['accepted'] and (r['afterReleaseBytes']==0 if r['accepted'] else r['afterRefusalBytes']==0),'No leaked ledger credit')
print(json.dumps({'retainedInspectionChecks':checks,'actualRecordedLocalProcesses':54,'completeWaveReferences':648,'processorReplayed':False,'numpyReplayed':False,'nativeAudio':False,'fullQualityQualified':False}))
