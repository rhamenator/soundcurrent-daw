#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent Fraction oracle against the actual bounded C++ map planner."""
from pathlib import Path
from fractions import Fraction as F
import argparse
import copy
import hashlib
import json
import random
import subprocess
import time
import uuid as uuid_module

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('probe', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--new-run-under', action='store_true', help='Create an exclusive child under the supplied evidence directory for repeated CTest runs')
args = parser.parse_args()
output = args.output.resolve()
if args.new_run_under:
    output.mkdir(parents=True, exist_ok=True)
    output = output / ('run-' + str(uuid_module.uuid4()))
output.mkdir(parents=True, exist_ok=False)
probe = args.probe.resolve()
rng = random.Random(20261010)
MAX = 2**63 - 1
U64 = 2**64 - 1
requests, contracts = [], []
checks = 0


def triple(x):
    x = F(x)
    return [x.numerator // x.denominator, x.numerator % x.denominator, x.denominator]


def value(x):
    return F(x[0]) + F(x[1], x[2])


def uuid(n):
    text = f'{n+1:032x}'
    return '-'.join([text[:8],text[8:12],text[12:16],text[16:20],text[20:]])


def spec(frames=32768, target=49152, markers=None, origin=F(17,2), visible=None):
    return {'rawOrigin':triple(origin), 'physicalRate':48000,
            'availableSourceFrames':int(origin)+frames+8, 'inputFrames':frames, 'outputFrames':target,
            'visibleBegin':triple(visible[0] if visible else F(frames,4)),
            'visibleEnd':triple(visible[1] if visible else F(frames*3,4)),
            'markers':markers or [], 'forward':[], 'inverse':[]}


def add(j, kind, accepted=True):
    requests.append(j)
    contracts.append({'kind':kind, 'accepted':accepted})


def piecewise(j, x, inverse=False):
    points = [(F(0),F(0))]+sorted((value(p['source']),value(p['output'])) for p in j['markers'])+[(F(j['inputFrames']),F(j['outputFrames']))]
    if inverse:
        points = [(b,a) for a,b in points]
    if x == points[-1][0]:
        return points[-1][1]
    for (a,b),(c,d) in zip(points, points[1:]):
        if a <= x < c:
            return b+(x-a)*(d-b)/(c-a)
    raise AssertionError('Independent lookup outside domain')


for index in range(480):
    frames = rng.randrange(8192,32769)
    target = rng.randrange(frames//4,frames*4+1)
    count = index % 8
    sources = sorted(F(x,8) for x in rng.sample(range(1,frames*8),count))
    targets = sorted(F(x,8) for x in rng.sample(range(1,target*8),count))
    markers = [{'id':uuid(k),'source':triple(a),'output':triple(b)} for k,(a,b) in enumerate(zip(sources,targets))]
    rng.shuffle(markers)
    j = spec(frames,target,markers,origin=F(rng.randrange(0,64),8))
    x = [F(0),F(frames)]+sources+[F(rng.randrange(frames*16+1),16) for _ in range(8)]
    y = [piecewise(j,p) for p in x]
    j['forward'] = list(map(triple,x))
    j['inverse'] = list(map(triple,y))
    add(j,'random-fractional-monotone')

# Large values exercise portable integer math, exact identity and denominators.
for frames,target in [(MAX,MAX),(MAX,MAX-1),(MAX//4,MAX-3),(10**15,10**15*3//2),(8193,12290),(1,4)]:
    j=spec(frames,target,origin=0,visible=(0,frames));j['availableSourceFrames']=frames
    j['forward']=list(map(triple,[0,F(frames,2),F(frames)-F(1,3),frames]))
    j['inverse']=[triple(piecewise(j,value(v))) for v in j['forward']]
    add(j,'large/endpoints')
for denominator in [U64,U64-2,2**63+1]:
    j=spec(MAX,MAX,origin=0,visible=(0,MAX));j['availableSourceFrames']=MAX
    marker=F(7)+F(1,denominator)
    j['markers']=[{'id':uuid(1),'source':triple(marker),'output':triple(marker)}]
    j['forward']=list(map(triple,[F(1,denominator),marker,F(MAX)-F(1,denominator)]))
    j['inverse']=j['forward'][:]
    add(j,'large-denominator-identity')

base=spec(markers=[{'id':uuid(0),'source':triple(8192),'output':triple(10000)},
                   {'id':uuid(1),'source':triple(16384),'output':triple(27000)}])
mutations = [
 ('zero-input',lambda j:j.update(inputFrames=0)),
 ('zero-output',lambda j:j.update(outputFrames=0)),
 ('bad-rate',lambda j:j.update(physicalRate=7999)),
 ('grid-beyond-asset',lambda j:j.update(availableSourceFrames=32768)),
 ('visible-reverse',lambda j:j.update(visibleBegin=triple(25000),visibleEnd=triple(12000))),
 ('visible-outside',lambda j:j.update(visibleEnd=triple(32769))),
 ('marker-at-origin',lambda j:j['markers'][0].update(source=triple(0))),
 ('marker-at-end',lambda j:j['markers'][0].update(source=triple(32768))),
 ('output-at-origin',lambda j:j['markers'][0].update(output=triple(0))),
 ('output-at-end',lambda j:j['markers'][0].update(output=triple(49152))),
 ('duplicate-source',lambda j:j['markers'][1].update(source=triple(8192))),
 ('duplicate-output',lambda j:j['markers'][1].update(output=triple(10000))),
 ('reverse-output',lambda j:j['markers'][1].update(output=triple(5000))),
 ('duplicate-id',lambda j:j['markers'][1].update(id=uuid(0))),
 ('nil-id',lambda j:j['markers'][0].update(id='00000000-0000-0000-0000-000000000000')),
 ('negative-field',lambda j:j.update(inputFrames=-1)),
 ('float-field',lambda j:j.update(inputFrames=32768.0)),
 ('integer-overflow',lambda j:j.update(inputFrames=2**63)),
 ('zero-denominator',lambda j:j.update(rawOrigin=[0,0,0])),
 ('improper-fraction',lambda j:j.update(rawOrigin=[0,2,2])),
 ('marker-cap',lambda j:j.update(maximumMarkers=1)),
 ('payload-cap',lambda j:j.update(maximumPayloadBytes=1)),
 ('shared-ledger-cap',lambda j:j.update(ledgerBytes=1)),
 ('forward-outside',lambda j:j.update(forward=[triple(32769)])),
 ('inverse-outside',lambda j:j.update(inverse=[triple(49153)])),
]
for kind,mutate in mutations:
    j=copy.deepcopy(base);mutate(j);add(j,kind,False)
# Explicit conservative representation refusal; no floating repair.
j=spec(origin=F(1,U64),markers=[{'id':uuid(0),'source':triple(F(1,U64-2)),'output':triple(2)}]);add(j,'common-denominator-overflow',False)
j=spec(MAX,MAX-1,origin=0,visible=(0,MAX));j['availableSourceFrames']=MAX
j['markers']=[{'id':uuid(0),'source':triple(F(MAX)-F(1,3)),'output':triple(MAX-2)}];add(j,'segment-ratio-overflow',False)

started=time.monotonic()
payload=''.join(json.dumps(j,separators=(',',':'))+'\n' for j in requests)
child=subprocess.Popen([str(probe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:
    stdout,stderr=child.communicate(payload,timeout=30)
except subprocess.TimeoutExpired:
    child.kill();stdout,stderr=child.communicate()
    (output/'failure.json').write_text(json.dumps({'pid':child.pid,'exitCode':child.returncode,'timeout':True})+'\n')
    raise
(output/'requests.json').write_text(json.dumps(requests,indent=2)+'\n')
(output/'contracts.json').write_text(json.dumps(contracts,indent=2)+'\n')
(output/'stdout.jsonl').write_text(stdout)
(output/'stderr.log').write_text(stderr)
records=[json.loads(line) for line in stdout.splitlines()]


def check(condition,message):
    global checks
    checks+=1
    if not condition:
        raise AssertionError(message)


try:
    check(child.returncode==0 and stderr=='' and len(records)==len(requests),'Actual C++ oracle process')
    for j,contract,r in zip(requests,contracts,records):
        check(r['accepted']==contract['accepted'],contract['kind']+': '+str(r))
        if not r['accepted']:
            check(r['afterRefusalBytes']==0,'Refused map retained resource credit')
            continue
        check(r['afterReleaseBytes']==0 and r['chargedBytes']>0,'Map resource lifetime')
        expected=sorted(j['markers'],key=lambda p:value(p['source']))
        check([p['id'] for p in r['points']]==[None]+[p['id'] for p in expected]+[None],'Stable identity/order/endpoints')
        check([p['source'] for p in r['points']]==[triple(0)]+[triple(value(p['source'])) for p in expected]+[triple(j['inputFrames'])],'Canonical source points')
        check([p['output'] for p in r['points']]==[triple(0)]+[triple(value(p['output'])) for p in expected]+[triple(j['outputFrames'])],'Canonical output points')
        for x,actual,back,raw in zip(j['forward'],r['forward'],r['inverse'],r['raw']):
            want=piecewise(j,value(x));check(actual==triple(want),'Forward exact Fraction oracle')
            check(back==triple(value(x)),'Exact inverse round trip')
            check(raw==triple(value(j['rawOrigin'])+value(x)),'Absolute raw domain')
        check(r['visibleBegin']==triple(piecewise(j,value(j['visibleBegin']))) and r['visibleEnd']==triple(piecewise(j,value(j['visibleEnd']))),'Visible interval map')
        integer_markers=all(value(p['source']).denominator==value(p['output']).denominator==1 for p in expected)
        check(r['vendorAccepted']==integer_markers,'No fractional vendor rounding')
        if integer_markers:
            check(r['vendor']==[[int(value(p['source'])),int(value(p['output']))] for p in expected],'Integer interior-only vendor map')
    summary={'format':'sc-warp-map-geometry-oracle-v1','requests':len(requests),'accepted':sum(r['accepted'] for r in records),'refused':sum(not r['accepted'] for r in records),'checks':checks,
             'pid':child.pid,'exitCode':child.returncode,'seconds':time.monotonic()-started,'actualCppExecuted':True,'nativeAudio':False,'shippingMapImplemented':False,
             'probeSha256':hashlib.sha256(probe.read_bytes()).hexdigest(),'oracleSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'evidenceDirectory':str(output)}
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
except BaseException as error:
    (output/'failure.json').write_text(json.dumps({'error':repr(error),'pid':child.pid,'exitCode':child.returncode,'checks':checks},indent=2)+'\n')
    raise
