#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect retained planner/candidate observations; no DSP/native/audio replay."""
from pathlib import Path,PurePosixPath
from fractions import Fraction as F
import array,hashlib,json,math,struct,sys,zipfile
folder=Path(__file__).resolve().parent;sha=lambda data:hashlib.sha256(data).hexdigest();checks=0
def check(value,message):
    global checks
    checks+=1
    if not value:raise AssertionError(message)
def position(v):return F(v[0])+F(v[1],v[2])
def triple(x):return [x.numerator//x.denominator,x.numerator%x.denominator,x.denominator]
def mapping(points,x,inverse=False):
    nodes=[(position(p['source']),position(p['output'])) for p in points]
    if inverse:nodes=[(b,a) for a,b in nodes]
    if x==nodes[-1][0]:return nodes[-1][1]
    for (a,b),(c,d) in zip(nodes,nodes[1:]):
        if a<=x<c:return b+(x-a)*(d-b)/(c-a)
    raise AssertionError('Captured query outside map')
manifest=json.loads((folder/'manifest.json').read_bytes());archive=folder/'capture.zip'
check(manifest['format']=='sc-warp-map-local-capture-v1','Capture format')
check(archive.stat().st_size==manifest['bytes']<16*1024*1024 and sha(archive.read_bytes())==manifest['sha256'],'Archive hash/budget')
for key in ['shippingProcessorChanged','nativeWindowsReplayed','nativeAudio','fullQualityQualified','productBinaryUploaded']:check(manifest[key] is False,'Capture scope')
with zipfile.ZipFile(archive) as z:
    infos=z.infolist();check(len(infos)==len(set(z.namelist()))==len(manifest['entries']),'Member count')
    check(set(z.namelist())==set(manifest['entries']) and sum(i.file_size for i in infos)<48*1024*1024,'Archive payload')
    for i in infos:
        p=PurePosixPath(i.filename);check(not p.is_absolute() and '..' not in p.parts and '\\' not in i.filename and i.file_size<4*1024*1024,'Member admission')
        data=z.read(i);check(len(data)==manifest['entries'][i.filename]['bytes'] and sha(data)==manifest['entries'][i.filename]['sha256'],'Member hash')
    get=lambda name:json.loads(z.read(name))
    build=get('build-inputs.json');check(build['productionBase']=='a40e0b5fb1430ca1f3bd42793fb870c3b918b8ab' and build['sourceWasUncommitted'],'Working-source base')
    for scope in ['geometry-initial','geometry-final']:
        requests,contracts,summary=[get(scope+'/'+name) for name in ['requests.json','contracts.json','summary.json']]
        responses=[json.loads(line) for line in z.read(scope+'/stdout.jsonl').decode().splitlines()]
        check(len(requests)==len(contracts)==len(responses)==summary['requests']==516,'Geometry request count')
        check(summary['accepted']==489 and summary['refused']==27 and summary['checks']==23083 and summary['pid']>0 and summary['exitCode']==0 and summary['actualCppExecuted'],'Recorded C++ geometry outcome')
        check(z.read(scope+'/stderr.log')==b'' and summary['nativeAudio'] is False and summary['shippingMapImplemented'] is False,'Geometry scope')
        prefix='executed-initial/' if scope=='geometry-initial' else 'experiment/'
        check(sha(z.read(prefix+'geometry_oracle.py'))==summary['oracleSha256'],'Executed oracle source')
        for j,c,r in zip(requests,contracts,responses):
            check(r['accepted']==c['accepted'],'Geometry contract')
            if not r['accepted']:
                check(r['afterRefusalBytes']==0,'Failed map credit');continue
            check(r['afterReleaseBytes']==0 and r['chargedBytes']>0,'Map resource lifetime')
            check(r['points'][0]=={'id':None,'source':[0,0,1],'output':[0,0,1]} and r['points'][-1]=={'id':None,'source':[j['inputFrames'],0,1],'output':[j['outputFrames'],0,1]},'Implicit endpoints')
            ordered=sorted(j['markers'],key=lambda m:position(m['source']))
            check([v['id'] for v in r['points'][1:-1]]==[v['id'] for v in ordered],'Stable marker identities/order')
            for x,y,back,raw in zip(j['forward'],r['forward'],r['inverse'],r['raw']):
                check(y==triple(mapping(r['points'],position(x))),'Independent forward mapping')
                check(back==triple(mapping(r['points'],position(y),True))==triple(position(x)),'Independent inverse/roundtrip')
                check(raw==triple(position(j['rawOrigin'])+position(x)),'Raw coordinate')
            check(r['visibleBegin']==triple(mapping(r['points'],position(j['visibleBegin']))) and r['visibleEnd']==triple(mapping(r['points'],position(j['visibleEnd']))),'Visible map bounds')
            integer=all(position(v['source']).denominator==position(v['output']).denominator==1 for v in ordered)
            check(r['vendorAccepted']==integer,'Fractional vendor refusal')
            if integer:check(r['vendor']==[[int(position(v['source'])),int(position(v['output']))] for v in ordered],'Interior-only vendor coordinates')
    summary=get('candidates/summary.json');records=get('candidates/processes.json');analysis=get('candidates/analysis.json');initial=get('candidates/analysis-initial.json')
    check(len(records)==summary['actualProcesses']==summary['terminalSuccesses']==summary['exactDrains']==52,'Candidate outcomes')
    check(summary['seconds']<summary['globalDeadlineSeconds']==60 and summary['addressSpaceCeilingBytes']==512*1024*1024,'Recorded candidate budgets')
    check(summary['sourceCommit']==build['productionBase'] and summary['sourceWasUncommitted'],'Candidate source scope')
    check(sha(z.read('executed-initial/run_candidates.py'))==summary['runnerSha256'] and build['executables']['candidateRenderer']['sha256']==summary['probeSha256'],'Candidate code identity')
    for key in ['nativeAudio','shippingProcessorChanged','QStretchPassed','groupPhaseQualified']:check(summary[key] is False,'Candidate scope')
    waves={}
    for record in records:
        check(record['pid']>0 and record['exitCode']==0 and record['seconds']<10 and record['stderr']=='' and record['warnings']==0,'Terminal candidate process')
        result=get(f"candidates/case-{record['id']:02d}/render.json")
        check(result==record['result']==json.loads(record['stdout']) and result['request']==record['request'],'Process/metadata binding')
        check(result['exactDrain'] and result['writtenFrames']==result['target']==24576 and result['frames']==16384,'Finite exact drain observation')
        for kind,frames in [('source',16384),('rendered',24576)]:
            digest=record['sourceSha256' if kind=='source' else 'renderSha256'];data=z.read('waves/'+digest+'.wav');check(sha(data)==digest,'Candidate WAV hash')
            check(data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt ' and struct.unpack_from('<I',data,4)[0]==len(data)-8 and data[36:40]==b'data','Owned RIFF format')
            encoding,channels,rate,byte_rate,align,bits=struct.unpack_from('<HHIIHH',data,20)
            check(encoding==3 and channels==record['request']['channels'] and rate==48000 and bits==32 and align==channels*4 and byte_rate==48000*channels*4,'WAVE layout/rate')
            check(len(data)==44+frames*channels*4 and struct.unpack_from('<I',data,40)[0]==frames*channels*4,'WAVE exact size')
            samples=array.array('f');samples.frombytes(data[44:])
            if sys.byteorder!='little':samples.byteswap()
            check(samples.itemsize==4 and all(math.isfinite(x) for x in samples),'Finite candidate PCM')
            if kind=='rendered':
                check(math.isclose(max(map(abs,samples)),result['peakLinear'],rel_tol=1e-12,abs_tol=1e-12),'Candidate peak');waves[record['id']]=samples
    comparisons=analysis['partitionComparisons'];check(len(comparisons)==24 and sum(c['exactSampleBytes'] for c in comparisons)==20,'Partition scope')
    for c in comparisons:
        a,b=waves[c['case']],waves[c['partner']];check((a.tobytes()==b.tobytes())==c['exactSampleBytes'],'Partition exact samples')
        delta=[float(x)-float(y) for x,y in zip(a,b)];check(math.isclose(max(map(abs,delta)),c['maxSampleDifference'],rel_tol=1e-10,abs_tol=1e-10),'Partition maximum difference')
    for c in analysis['groupPolicyComparisons']:
        a,b=waves[c['apartCase']],waves[c['togetherCase']];check((a.tobytes()==b.tobytes())==c['exactSampleBytes'],'Recorded channel policy comparison')
    check(sha(z.read('experiment/analyze_candidates.py'))==analysis['analysisScriptSha256'],'Corrected analysis source')
    check(sha(z.read('executed-initial/analyze_candidates.py'))==initial['analysisScriptSha256'],'Initial analysis retained')
    observations=[o for row in analysis['observations'] for event in row['events'] for o in event['channels']]
    check(len(observations)==656 and sum(o['timingEstimateResolved'] for o in observations)==622 and sum(not o['timingEstimateResolved'] for o in observations)==34,'Censored onset scope')
    for o in observations:
        s,r=o['sourceDetector'],o['outputDetector'];resolved=not(s['windowStartCensored'] or s['windowEndCensored'] or r['windowStartCensored'] or r['windowEndCensored'])
        check(resolved==o['timingEstimateResolved'] and r['windowStartCensored']==(r['frame']==r['window'][0]),'Censored-window interpretation')
    for key in ['onsetEstimatorIsUniversalGroundTruth','fullQualityQualified','groupPhaseQualified','QStretchPassed','QPitchPassed']:check(analysis[key] is False,'No unsupported quality claim')
print(json.dumps({'retainedInspectionChecks':checks,'geometryRuns':2,'geometryRequestsPerRun':516,'fullCandidateWaves':52,'actualDSPReplayed':False,'nativeAudio':False,'fullQualityQualified':False}))
