#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check portable stretch retiming against independent exact Python rationals."""
from fractions import Fraction
import json, math, random, subprocess, sys
LIMIT=(1<<63)-1
UINT=(1<<64)-1
rng=random.Random(20261009)
cases=[]
for frame in [-(1<<63),-9007199254741017,-100,-1,0,1,100,9007199254741017,LIMIT]:
    for n,d in [(1,1),(1,4),(4,1),(3,2),(UINT,UINT),(UINT,1),(1,UINT),(UINT-1,UINT),(UINT,UINT-1),(0,1),(1,0)]:
        for ceiling in [False,True]:cases.append(dict(kind='frame',frame=frame,n=n,d=d,ceiling=ceiling))
for _ in range(700):
    cases.append(dict(kind='frame',frame=rng.randint(-(1<<63),LIMIT),n=rng.randint(1,UINT),d=rng.randint(1,UINT),ceiling=bool(rng.getrandbits(1))))
for frame in [0,17,(1<<53)+19,LIMIT]:
    for f,den in [(0,1),(1,3),(6,7),(UINT-1,UINT)]:
        for n,d in [(1,1),(1,4),(4,1),(3,2),(UINT,UINT),(0,1),(1,0)]:
            cases.append(dict(kind='position',frame=frame,fraction=f,denominator=den,n=n,d=d))
for _ in range(650):
    den=rng.randint(1,1000000)
    cases.append(dict(kind='position',frame=rng.choice([rng.randint(0,1000000000),(1<<53)+19,LIMIT-rng.randint(0,1000)]),fraction=rng.randrange(den),denominator=den,n=rng.randint(1,1000000000),d=rng.randint(1,1000000000)))
cases += [dict(kind='position',frame=0,fraction=1,denominator=0,n=1,d=1),dict(kind='position',frame=-1,fraction=0,denominator=1,n=1,d=1),dict(kind='position',frame=0,fraction=3,denominator=3,n=1,d=1)]
raw=''.join(json.dumps(c)+'\n' for c in cases)
p=subprocess.run([sys.argv[1],'--arithmetic-probe'],input=raw,text=True,capture_output=True,timeout=20)
assert p.returncode==0,p.stderr
answers=[json.loads(line) for line in p.stdout.splitlines()];assert len(answers)==len(cases)
accepted=refused=intermediate=0
for c,a in zip(cases,answers):
    valid=c['n']>0 and c['d']>0
    if c['kind']=='position':valid=valid and c['frame']>=0 and c['denominator']>0 and 0<=c['fraction']<c['denominator']
    if valid:
        x=Fraction(c['frame'])
        if c['kind']=='position':x+=Fraction(c['fraction'],c['denominator'])
        x*=Fraction(c['n'],c['d'])
        if c['kind']=='frame':expected=dict(accepted=True,frame=math.ceil(x) if c['ceiling'] else math.floor(x));representable=-(1<<63)<=expected['frame']<=LIMIT
        else:
            whole=math.floor(x);frac=x-whole
            expected=dict(accepted=True,frame=whole,fraction=frac.numerator,denominator=frac.denominator)
            representable=whole<=LIMIT and frac.denominator<=UINT
    else:representable=False
    if a['accepted']:
        assert valid and representable and a==expected,(c,a,expected)
        accepted+=1
    else:
        refused+=1
        if valid and representable:
            # Documented intermediate denominator admission; ordinary bounded
            # origins/ratios above must all be accepted. No floating rounding.
            assert c['kind']=='position' and c['denominator']*c['d']//math.gcd(c['n'],c['d'])>UINT,(c,a,expected)
            intermediate+=1
assert accepted>1000 and refused>100
print(json.dumps(dict(cases=len(cases),accepted=accepted,refused=refused,intermediateDenominatorRefusals=intermediate,independentArbitraryPrecision=True,exactAcceptedCoordinates=True)))
