#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent Fraction geometry and refusal admission, exercised on native binaries."""
from pathlib import Path
from fractions import Fraction as F
import argparse, copy, datetime, hashlib, json, subprocess, time
p = argparse.ArgumentParser()
p.add_argument('probe', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--new-run-under', action='store_true')
args = p.parse_args()
root = args.output.resolve()
if args.new_run_under:
    root.mkdir(parents=True, exist_ok=True)
    root = root/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
root.mkdir(parents=True, exist_ok=False)
pos = lambda x: [F(x).numerator//F(x).denominator, F(x).numerator%F(x).denominator, F(x).denominator]
fraction = lambda x: F(x[0])+F(x[1], x[2])
ident = lambda n: f'00000000-0000-0000-0000-{n:012x}'
def request(pairs, frames=32768, target=49152):
    return {'rawOrigin': [0, 0, 1], 'physicalRate': 48000, 'availableSourceFrames': frames, 'inputFrames': frames, 'outputFrames': target,
            'visibleBegin': [0, 0, 1], 'visibleEnd': [frames, 0, 1], 'forward': [], 'inverse': [],
            'markers': [{'id': ident(i+1), 'source': pos(a), 'output': pos(b)} for i, (a, b) in enumerate(pairs)]}
base = request([(4096,6144),(12288,16384),(20480,32768),(28672,43008)])
cases = []
def case(name, value, accepted=True):
    cases.append({'name': name, 'request': value, 'accepted': accepted})
case('nonuniform', base)
case('identity', request([(4096,4096),(12288,12288)], target=32768))
case('touching spans', request([(4096,4096),(6400,6400)], target=32768))
case('first span starts at endpoint', request([(256,256)], target=32768))
case('last span ends at endpoint', request([(30720,47104)]))
case('short unity gap', request([(257,257)], target=32768))
case('minimum nonunity gap', request([(320,321)]))
case('fractional exact map, integer adapter refusal', request([(F(8193,2),F(12289,2))]))
reverse = copy.deepcopy(base); reverse['markers'].reverse(); case('unordered IDs preserved', reverse)
custom = request([(32,32),(192,192)], frames=1000, target=1000)
custom['policy'] = {'before':32,'after':128,'halo':16,'minimumGap':64};case('custom policy and touching cores',custom)
case('short nonunity gap', request([(319,320)]), False)
case('first span exceeds source start', request([(255,6144)]), False)
case('first span exceeds output start', request([(4096,255)]), False)
case('last span exceeds source end', request([(30721,43008)]), False)
case('last span exceeds output end', request([(28672,47105)]), False)
case('collapsed source gap', request([(256,300)]), False)
case('collapsed output gap', request([(300,256)]), False)
case('source span overlap', request([(4096,6144),(6000,10000)]), False)
case('output span overlap', request([(4096,6144),(12288,8000)]), False)
case('touching source, separated output', request([(4096,6144),(6400,10000)]), False)
case('reversed output', request([(4096,10000),(12288,6144)]), False)
case('duplicate source', request([(4096,6144),(4096,10000)]), False)
dupe = copy.deepcopy(base);dupe['markers'][1]['id'] = dupe['markers'][0]['id'];case('duplicate owner',dupe,False)
bad = copy.deepcopy(base);bad['markers'][0]['id'] = 'not-a-uuid';case('invalid owner',bad,False)
bad=copy.deepcopy(base);bad['markers'][0]['id']='00000000-0000-0000-0000-000000000000';case('nil owner',bad,False)
case('empty marker bank',request([]),False)
for name, key, value in [('count admission','maximumMarkers',2),('payload admission','maximumPayloadBytes',1024),('ledger admission','ledgerBytes',100),('nested ledger rollback','ledgerBytes',6000)]:
    c=copy.deepcopy(base);c[key]=value;case(name,c,False)
for name, policy in [('no core',{'before':64,'after':2048,'halo':64,'minimumGap':64}),('negative halo',{'before':256,'after':2048,'halo':-1,'minimumGap':64}),('zero minimum gap',{'before':256,'after':2048,'halo':64,'minimumGap':0})]:
    c=copy.deepcopy(base);c['policy']=policy;case(name,c,False)
bad=copy.deepcopy(base);bad['markers'][0]['source']=[4096,1,0];case('zero denominator',bad,False)
bad=copy.deepcopy(base);bad['rawOrigin']=[0,1,2];case('raw fractional source grid',bad)
checks=0
def check(value, message):
    global checks
    checks+=1
    assert value,message
def expected(c):
    j=c['request'];policy=j.get('policy',{'before':256,'after':2048,'halo':64,'minimumGap':64})
    markers=sorted(j['markers'],key=lambda v:fraction(v['source']))
    spans=[];points=[(F(0),F(0))];gaps=[];last=(F(0),F(0))
    for marker in markers:
        a,b=fraction(marker['source']),fraction(marker['output']);sb,se=a-policy['before'],a+policy['after'];ob,oe=b-policy['before'],b+policy['after']
        spans.append({'owner':marker['id'],'sourceAnchor':pos(a),'outputAnchor':pos(b),'sourceBegin':pos(sb),'sourceEnd':pos(se),'outputBegin':pos(ob),'outputEnd':pos(oe)})
        if (sb,ob)!=last:gaps.append({'sourceBegin':pos(last[0]),'sourceEnd':pos(sb),'outputBegin':pos(last[1]),'outputEnd':pos(ob)})
        for v in [(sb,ob),(a,b),(se,oe)]:
            if v!=points[-1]:points.append(v)
        last=(se,oe)
    endpoint=(F(j['inputFrames']),F(j['outputFrames']))
    if endpoint!=last:gaps.append({'sourceBegin':pos(last[0]),'sourceEnd':pos(endpoint[0]),'outputBegin':pos(last[1]),'outputEnd':pos(endpoint[1])})
    if endpoint!=points[-1]:points.append(endpoint)
    return spans,gaps,points
def mapping(points,x,inverse=False):
    if inverse:points=[(b,a) for a,b in points]
    if x==points[-1][0]:return points[-1][1]
    for (a,b),(c,d) in zip(points,points[1:]):
        if a<=x<c:return b+(x-a)*(d-b)/(c-a)
    raise AssertionError('Oracle outside domain')
for c in cases:
    if c['accepted']:
        _,_,points=expected(c)
        c['request']['forward']=[pos(v) for v in sorted(set([a for a,b in points]+[(a+c)/2 for (a,b),(c,d) in zip(points,points[1:])]))]
        c['request']['inverse']=[pos(v) for v in sorted(set([b for a,b in points]+[(b+d)/2 for (a,b),(c,d) in zip(points,points[1:])]))]
(root/'requests.json').write_text(json.dumps(cases,indent=2)+'\n')
probe=args.probe.resolve();started=time.monotonic();child=subprocess.Popen([str(probe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
out,err=child.communicate(''.join(json.dumps(c['request'])+'\n' for c in cases),timeout=30)
(root/'stdout.jsonl').write_text(out);(root/'stderr.log').write_text(err)
check(child.returncode==0 and err=='','Terminal geometry process')
results=[json.loads(line) for line in out.splitlines()];check(len(results)==len(cases),'Complete bank')
for c,r in zip(cases,results):
    check(r['accepted']==c['accepted'],c['name']+': '+str(r))
    if not c['accepted']:
        check(r['afterRefusalBytes']==0 and r['error'],c['name']+' rollback');continue
    spans,gaps,points=expected(c);check(r['spans']==spans and r['gaps']==gaps,c['name']+' owner/boundaries')
    check([(fraction(v['source']),fraction(v['output'])) for v in r['points']]==points,c['name']+' derived geometry')
    check(r['afterReleaseBytes']==0 and r['chargedBytes']>0,c['name']+' retirement')
    for name,inverse in [('forward',False),('inverse',True)]:
        for x,y in zip(c['request'][name],r[name]):check(fraction(y)==mapping(points,fraction(x),inverse),c['name']+' exact lookup')
        check(len(r[name])==len(c['request'][name]),c['name']+' complete lookup')
    raw=fraction(c['request']['rawOrigin']);check([fraction(v) for v in r['raw']]==[raw+fraction(v) for v in c['request']['forward']],c['name']+' physical origin')
    integer=all(a.denominator==b.denominator==1 for a,b in points);check(r['vendorAccepted']==integer,c['name']+' no rounding')
    if integer:check(r['vendor']==[[int(a),int(b)] for a,b in points[1:-1]],c['name']+' omit endpoints')
summary={'format':'sc-protected-warp-geometry-run-v1','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':child.pid,'exitCode':child.returncode,'seconds':time.monotonic()-started,
         'requests':len(cases),'accepted':sum(c['accepted'] for c in cases),'refused':sum(not c['accepted'] for c in cases),'independentChecks':checks,
         'probeSha256':hashlib.sha256(probe.read_bytes()).hexdigest(),'oracleSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'processorReplayed':False,'nativeAudio':False}
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
