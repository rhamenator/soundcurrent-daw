#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent large-ratio Fraction coordinates, without native integer products."""
from fractions import Fraction
from pathlib import Path
import argparse,json,subprocess,tempfile,hashlib
p=argparse.ArgumentParser();p.add_argument('fixture');p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
def triple(f):
 w=f.numerator//f.denominator;r=f-w;return [w,r.numerator,r.denominator]
def value(t):return Fraction(t[0])+Fraction(t[1],t[2])
cases=[]
for origin in [Fraction(0),Fraction(4096)+Fraction(1,7),Fraction(2**53+11)+Fraction(3,11),Fraction(2**63-65536)]:
 for span in [Fraction(8192)+Fraction(1,1000003),Fraction(4096)+Fraction(17,1000033),Fraction(16384)+Fraction(13,999983)]:
  for count in [4096,8193,16384]:cases.append(dict(origin=triple(origin),span=triple(span),frames=count,offsets=[0,1,37,count//2,count-1,count]))
# The product 7*(2**63-1) requires the portable wide fallback; the result fits.
cases.append(dict(origin=triple(Fraction(0)),span=triple(Fraction(7,11)),frames=1,offsets=[0,1,2,2**63-1]))
a.evidence.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence));raw=json.dumps(cases).encode();(root/'input.json').write_bytes(raw)
binary=Path(a.fixture).resolve();digest=hashlib.sha256(binary.read_bytes()).hexdigest();r=subprocess.run([str(binary),'--oracle'],input=raw,capture_output=True,timeout=30)
(root/'stdout.json').write_bytes(r.stdout);(root/'stderr.log').write_bytes(r.stderr);assert r.returncode==0,r.stderr
rows=json.loads(r.stdout);assert len(rows)==len(cases);assertions=0
for c,row in zip(cases,rows):
 assert 'refused' not in row
 origin=value(c['origin']);span=value(c['span']);step=span/c['frames']
 assert Fraction(*row['step'])==step;assertions+=1
 assert [value(x) for x in row['points']]==[origin+i*step for i in c['offsets']];assertions+=len(c['offsets'])
assert hashlib.sha256(binary.read_bytes()).hexdigest()==digest
report=dict(cases=len(cases),exactAssertions=assertions,fixtureSha256=digest,sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),exitCode=r.returncode)
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
