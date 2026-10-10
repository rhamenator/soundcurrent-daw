#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent arbitrary-precision oracle for actual C++ region/crop geometry."""
from fractions import Fraction
from pathlib import Path
import json, random, subprocess, sys

exe=Path(sys.argv[1]).resolve()
cases=[]
base={'available':60000,'first':4096,'fraction':1,'fractionDenominator':2,'frames':128,
      'n':3,'d':2,'pitch':0,'context':True,'before':4095,'after':4096}
def add(**values):cases.append(dict(base,**values))
for context in [False,True]:
    for fraction,den in [(0,1),(1,2),(2,6),(2**64-2,2**64-1)]:
        for n,d in [(1,1),(3,2),(13,6),(1,4),(4,1),(1000000,750000)]:
            add(context=context,fraction=fraction,fractionDenominator=den,n=n,d=d)
for change in [{'before':-1},{'after':-1},{'before':4097},{'after':60000},
               {'frames':0},{'frames':1000000001},{'before':1000000000},
               {'n':0},{'d':0},{'n':5,'d':1},{'n':1,'d':5},{'n':1000001},
               {'pitch':2400001},{'pitch':-2400001},{'fractionDenominator':0},
               {'fraction':2,'fractionDenominator':2},{'first':-1},{'available':0},
               {'first':60000},{'available':8320},
               {'first':500000000,'frames':500000000,'before':0,'after':0,'available':1000000000,'n':4,'d':1}]:
    add(**change)
add(first=2**63-11,available=2**63-1,frames=9,before=0,after=0)
add(first=250000000,available=1000000000,frames=249999999,before=1,after=0,n=4,d=1)
random.seed(0x5343524547494F4E)
for _ in range(900):
    available=random.randrange(1,1000001);first=random.randrange(available)
    frames=random.randrange(1,available-first+1);before=random.randrange(first+2);after=random.randrange(available-first-frames+2)
    den=random.choice([1,2,3,97,2**32-1,2**64-1]);fraction=random.randrange(den)
    n,d=random.choice([(1,1),(3,2),(13,6),(1,4),(4,1),(999999,1000000)])
    add(available=available,first=first,frames=frames,before=before,after=after,n=n,d=d,
        fraction=fraction,fractionDenominator=den,context=random.choice([False,True]),pitch=random.choice([-2400000,0,700007,2400000]))

def position(value):
    frame=value.numerator//value.denominator;part=value-frame
    return [frame,part.numerator,part.denominator]
def oracle(c):
    first,frames,available=c['first'],c['frames'],c['available']
    before,after=(c['before'],c['after']) if c['context'] else (0,0)
    n,d=c['n'],c['d'];fraction,den=c['fraction'],c['fractionDenominator']
    valid=(0<=first<available and 0<frames<=1000000000 and den>0 and 0<=fraction<den and
           1<=n<=1000000 and 1<=d<=1000000 and n*4>=d and d*4>=n and
           -2400000<=c['pitch']<=2400000 and 0<=before<=first and after>=0 and
           frames+before+after<=1000000000 and frames+before+after<=available-(first-before))
    if not valid:return {'accepted':False}
    total=before+frames+after;nominal=Fraction(n,d)
    target=(total*n+d-1)//d
    if not 0<target<=1000000000:return {'accepted':False}
    mapping=nominal if c['context'] else Fraction(target,total)
    return {'accepted':True,'inputOrigin':position(Fraction(first-before)+Fraction(fraction,den)),
            'inputFrames':total,'outputFrames':target,'visibleBegin':position(before*mapping),
            'visibleEnd':position((before+frames)*mapping),
            'map':[nominal.numerator,nominal.denominator] if c['context'] else [target,total]}

run=subprocess.run([str(exe),'--geometry-probe'],input=''.join(json.dumps(c)+'\n' for c in cases),
                   text=True,capture_output=True,timeout=30)
assert run.returncode==0,(run.stdout,run.stderr)
rows=[json.loads(line) for line in run.stdout.splitlines()];assert len(rows)==len(cases)
for case,actual in zip(cases,rows):assert actual==oracle(case),(case,actual,oracle(case))
# Both nominal settings have the same globally rounded duration for this region,
# but must retain distinct visible maps and render identities.
same=[]
for n,d in [(3,2),(12479,8319)]:same.append(oracle(dict(base,n=n,d=d)))
assert same[0]['outputFrames']==same[1]['outputFrames']==12479
assert same[0]['visibleBegin']!=same[1]['visibleBegin']
assert oracle(base)['visibleEnd'][0]-oracle(base)['visibleBegin'][0]==192
print(json.dumps({'checks':len(cases)+4,'accepted':sum(r['accepted'] for r in rows),
    'refused':sum(not r['accepted'] for r in rows),'actualCppExecuted':True,
    'oracle':'Python arbitrary-precision Fraction','nominalVisibleDuration':True,
    'sameRoundedTargetDistinctMaps':True,'nativeAudio':False}))
