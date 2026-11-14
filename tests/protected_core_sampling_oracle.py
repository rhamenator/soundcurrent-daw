#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent exact Fraction oracle for cropped, phase-shifted protected cores."""
from fractions import Fraction as F
from pathlib import Path
import argparse,json,random,subprocess,tempfile,hashlib
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
a.evidence.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));rng=random.Random(308)
def encode(x):return [x.numerator//x.denominator,x.numerator%x.denominator,x.denominator]
def decode(x):return F(x[0])+F(x[1],x[2])
def ceil(x):return -((-x.numerator)//x.denominator)
rows=[];expected=[]
for index in range(240):
    origin=F(rng.choice([1024,2**53+11,2**63-1-65536]))+F(rng.randrange(7),7)
    anchors=[[F(8192)+F(rng.randrange(3),3),F(12288)+F(rng.randrange(5),5)],
             [F(24576)+F(rng.randrange(7),7),F(36864)+F(rng.randrange(11),11)]]
    source,output=rng.choice([(48000,48000),(48000,44100),(44100,48000),(48000,96000),(96000,48000),(192000,8000),(8000,192000)])
    step=F(source,output)
    # Include starts/ends inside a core, at an exact semantic boundary, in gaps,
    # and after the marker whose core is being cropped.
    begin=rng.choice([F(0),anchors[0][1]-192,anchors[0][1]+F(1,13),anchors[1][1]+F(33,7)])
    count=min(ceil((F(49152)-begin)/step),rng.choice([17,137,1000000]))
    row=dict(rawOrigin=encode(origin),sourceFrames=int(origin)+65536,markers=[[encode(s),encode(o)] for s,o in anchors],sourceRate=source,outputRate=output,clockOrigin=encode(begin),outputFrames=count)
    result=[]
    for n,(s,o) in enumerate(anchors):
        ob,oe=o-192,o+448;rb,re=origin+s-192,origin+s+448
        first=max(0,min(count,ceil((ob-begin)/step)));end=max(0,min(count,ceil((oe-begin)/step)))
        if first==end:continue
        result.append(dict(owner=f'00000000-0000-0000-0000-{n+1:012d}',first=first,end=end,rawBegin=rb,rawEnd=re,outputBegin=ob,outputEnd=oe,
                           firstRaw=origin+s+begin+first*step-o,lastRaw=origin+s+begin+(end-1)*step-o))
    rows.append(row);expected.append(result)
command=[str(a.fixture.resolve()),'--plan'];payload=json.dumps(rows);(root/'inputs.json').write_text(payload+'\n')
proc=subprocess.run(command,input=payload,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=60)
(root/'stdout').write_text(proc.stdout);(root/'stderr').write_text(proc.stderr);assert proc.returncode==0,proc.stderr
actual=json.loads(proc.stdout);assert len(actual)==len(expected);queries=0
for got,wanted in zip(actual,expected):
    assert len(got)==len(wanted)
    for g,w in zip(got,wanted):
        for k,v in w.items():
            assert (decode(g[k]) if isinstance(v,F) else g[k])==v,(k,g,w)
            queries+=1
result=dict(cases=len(rows),exactAssertions=queries,fixtureSha256=hashlib.sha256(a.fixture.read_bytes()).hexdigest(),sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),exitCode=proc.returncode,workerIntegrated=False,qualityAdmission=False)
(root/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
