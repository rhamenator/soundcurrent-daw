#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent Fraction oracle for exact semantic outer extents and map queries."""
from pathlib import Path
from fractions import Fraction as F
import argparse,hashlib,json,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--evidence',required=True,type=Path);a=p.parse_args()
a.evidence.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence))
def pos(x):
 x=F(x);whole=x.numerator//x.denominator;r=x-whole;return [whole,r.numerator,r.denominator]
def value(p):return F(p[0])+F(p[1],p[2])
inputs=[];expected=[]
for origin in [F(1024,1)+F(1,7),F(2**53)+F(2,11),F(2**63-32768)+F(1,3)]:
 for den in [3,5,7,11]:
  for compressed in [False,True]:
   for partial in [False,True]:
    extent=F(10000)+F(1,den);end=F(8000 if compressed else 12000)+F(2,den)
    source=F(4500)+F(1,13);target=F(3500 if compressed else 6000)+F(1,17)
    queries=[F(0),F(1,den),source/2,source-F(1,19),source,source+F(1,19),(source+extent)/2,extent-F(1,23),extent]
    if partial:queries=queries[2:-1] # same full map even when queries omit outer endpoints
    row=dict(origin=pos(origin),sourceFrames=int(origin)+20000,input=pos(extent),output=pos(end),anchorSource=pos(source),anchorOutput=pos(target),queries=[pos(q) for q in queries]);inputs.append(row)
    results=[]
    for q in queries:
     mapped=q*target/source if q<=source else target+(q-source)*(end-target)/(extent-source)
     results.append([pos(mapped),pos(q),pos(origin+q)])
    expected.append(dict(points=[[[0,0,1],[0,0,1]],[pos(source),pos(target)],[pos(extent),pos(end)]],rows=results))
raw=json.dumps(inputs).encode();(root/'inputs.json').write_bytes(raw)
r=subprocess.run([str(a.binary.resolve()),'--probe'],input=raw,capture_output=True);(root/'stdout.json').write_bytes(r.stdout);(root/'stderr.log').write_bytes(r.stderr)
report=dict(cases=len(inputs),exactAssertions=sum(len(e['rows'])*3+6 for e in expected),exitCode=r.returncode,inputSha256=hashlib.sha256(raw).hexdigest(),sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),binarySha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),allResponsesMatch=False)
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n');assert r.returncode==0,r.stderr.decode(errors='replace');actual=json.loads(r.stdout);assert actual==expected,'Exact extent/map result differs from independent Fraction oracle';report['allResponsesMatch']=True;(root/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
