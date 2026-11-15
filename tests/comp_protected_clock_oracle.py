#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent full-map/retained-clock verifier oracle, including omitted knots."""
from pathlib import Path
from fractions import Fraction as F
import argparse,json,hashlib,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence))
def pos(p):
 p=F(p);whole=p.numerator//p.denominator;frac=p-whole;return [whole,frac.numerator,frac.denominator]
origin=F(32768)+F(1,7);knots=[(F(0),F(0))]
for source,output in [(F(16384)+F(1,3),F(24576)+F(1,5)),(F(49152)+F(2,7),F(73728)+F(2,11))]:
 knots.extend([(source-256,output-256),(source,output),(source+512,output+512)])
knots.append((F(65536),F(98304)))
def raw(output):
 if output==knots[-1][1]:return origin+knots[-1][0]
 for (sa,oa),(sb,ob) in zip(knots,knots[1:]):
  if oa<=output<ob:return origin+sa+(output-oa)*(sb-sa)/(ob-oa)
 raise AssertionError('Oracle coordinate outside full domain')
inputs=[];expected=[]
for step in [F(147,160),F(1),F(2),F(3,2),F(441,320),F(3)]:
 for start in [F(0),F(1,7),F(28000)+F(2,11),F(72000)+F(1,13)]:
  for count in [4096,32768]:
   end=start+count*step
   if end>98304:continue
   points=[start]+[o for _,o in knots if start<o<end]+[end]
   inputs.append(dict(frames=count,map=[[pos(raw(o)),pos((o-start)/step)] for o in points]));expected.append(dict(accepted=True,origin=pos(start),step=pos(step),last=pos(start+(count-1)*step)))
# Full nonuniform range: endpoints alone must not prove the interior clock.
for omitted in [True,False]:
 start=F(0);end=F(90000);count=90000;points=[start]+[o for _,o in knots if start<o<end]+[end]
 mapping=[[pos(raw(o)),pos(o)] for o in points]
 if omitted:mapping=[mapping[0],mapping[-1]]
 else:mapping[2][0]=pos(raw(points[2])+F(1,97))
 inputs.append(dict(frames=count,map=mapping));expected.append(dict(accepted=False))
# Exact endpoint/count identity, monotonicity and canonical input remain required.
for mode in range(3):
 row=json.loads(json.dumps(inputs[0]))
 if mode==0:row['frames']+=1
 elif mode==1:row['map'][0][1]=[0,1,7]
 else:row['map'][-1][0]=row['map'][0][0]
 inputs.append(row);expected.append(dict(accepted=False))
rawInput=json.dumps(inputs).encode();(root/'inputs.json').write_bytes(rawInput)
r=subprocess.run([str(a.binary.resolve()),'--clock-oracle'],input=rawInput,capture_output=True);(root/'stdout.json').write_bytes(r.stdout);(root/'stderr.log').write_bytes(r.stderr)
report=dict(cases=len(inputs),acceptedCases=sum(e['accepted'] for e in expected),refusedCases=sum(not e['accepted'] for e in expected),exactAssertions=sum(4 if e['accepted'] else 1 for e in expected),exitCode=r.returncode,inputSha256=hashlib.sha256(rawInput).hexdigest(),sourceSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),binarySha256=hashlib.sha256(a.binary.read_bytes()).hexdigest(),allResponsesMatch=False)
(root/'report.json').write_text(json.dumps(report,indent=2)+'\n');assert r.returncode==0,r.stderr.decode(errors='replace');assert json.loads(r.stdout)==expected,'Retained-map clock differs from independent Fraction oracle';report['allResponsesMatch']=True;(root/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
