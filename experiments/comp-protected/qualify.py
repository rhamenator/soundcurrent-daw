#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent exact-map and protected-PCM checks for the isolated candidate."""
import argparse
import array
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('probe',type=Path)
p.add_argument('output',type=Path,help='Exclusive new evidence directory')
a=p.parse_args()
root=a.output.resolve();root.mkdir(parents=True,exist_ok=False)
exe=a.probe.resolve()
repo=Path(__file__).resolve().parents[2]
def digest(path):
    with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def snapshot():
    files=['CMakeLists.txt','experiments/comp-protected/probe.cpp','experiments/comp-protected/qualify.py',
           'src/protected_warp_render.cpp','src/comp_handle_plan.cpp','src/protected_warp.cpp','src/warp_map.cpp']
    return {'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
            'sources':{f:digest(repo/f) for f in files},'executableSha256':digest(exe)}
before=snapshot();command=[str(exe),str(root/'pcm')];start=time.monotonic()
with (root/'stdout.log').open('wb') as out,(root/'stderr.log').open('wb') as err:
    try:
        proc=subprocess.run(command,stdout=out,stderr=err,timeout=180)
        code=proc.returncode
    except subprocess.TimeoutExpired:
        code='timeout' # subprocess.run kills and reaps this owned child.
after=snapshot()
execution={'command':command,'exitCode':code,'seconds':time.monotonic()-start,'before':before,'after':after}
(root/'execution.json').write_text(json.dumps(execution,indent=2)+'\n')
assert code==0,(root,code)
assert before==after,'Source or executable changed during acceptance'
report=json.loads((root/'pcm/report.json').read_text())
assert len(report['rows'])==9
assert len(report['refusals'])==4
assert not report['shippingEnabled'] and not report['qualityAdmission'] and not report['nativeWindowsQualified']
def coord(t):return F(t[0])+F(t[1],t[2])
def points(row,key):return [(coord(y),coord(x)) for x,y in row[key]]
def mapped(ps,x):
    if x==ps[-1][0]:return ps[-1][1]
    for (a,b),(c,d) in zip(ps,ps[1:]):
        if a<=x<c:return b+(x-a)*(d-b)/(c-a)
    raise AssertionError(('Outside map',x))
checks=0;samples=0;observations=[]
for row in report['rows']:
    rate=row['rate'];extension=row['extension']
    assert row['prefix']==3008*extension and row['suffix']==4544*extension
    assert row['rawOrigin']==16384-1984*extension
    assert row['rawFrames']==32768+(1984+3008)*extension
    assert row['outputFrames']==49152+(3008+4544)*extension
    envelope=points(row,'envelope');candidate=points(row,'candidate')
    for ps in (envelope,candidate):
        assert ps[0]==(F(0),F(row['rawOrigin']))
        assert ps[-1]==(F(row['outputFrames']),F(row['rawOrigin']+row['rawFrames']))
        assert all(a<c and b<d for (a,b),(c,d) in zip(ps,ps[1:]))
    knots=sorted({x for x,y in envelope+candidate})
    queries=knots+[(a+b)/2 for a,b in zip(knots,knots[1:])]
    for x in queries:
        assert mapped(envelope,x)==mapped(candidate,x)
        checks+=1
    raw=(root/'pcm'/f'source-{rate}.f32le').read_bytes()
    rendered=(root/'pcm'/row['pcm']).read_bytes()
    assert len(raw)==98304*2*4 and len(rendered)==row['outputFrames']*2*4
    values=array.array('f');values.frombytes(rendered)
    if sys.byteorder!='little':values.byteswap()
    assert all(math.isfinite(x) for x in values)
    peak=max(abs(x) for x in values)
    assert peak>1,'Floating-point headroom was lost'
    for core in row['cores']:
        start=core['rawBegin'];out=core['outputBegin'];n=core['frames']
        assert start==16384+8192-256+64 and n==256+512-128
        assert out==row['prefix']+12288-256+64
        assert raw[start*8:(start+n)*8]==rendered[out*8:(out+n)*8]
        samples+=n*2
    assert row['processedGaps']==2 and row['polls']>row['writes']>0
    observations.append({'rate':rate,'extension':extension,'floatPeak':peak,'outputSha256':hashlib.sha256(rendered).hexdigest()})
manifest={f.relative_to(root).as_posix():{'bytes':f.stat().st_size,'sha256':digest(f)} for f in sorted(root.rglob('*')) if f.is_file()}
result={'format':'sc-comp-protected-feasibility-v1','renders':9,'exactMapQueries':checks,
        'bitExactProtectedSamples':samples,'refusals':report['refusals'],'observations':observations,
        'sourceAndExecutableStable':True,'files':manifest,'processingQualityQualified':False,
        'publicationOrUiIntegrated':False,'nativeWindowsQualified':False}
(root/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('files','observations')}))
