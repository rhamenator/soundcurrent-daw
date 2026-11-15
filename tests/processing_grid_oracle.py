#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent Fraction coordinates for worker grids, without clip speed limits."""
import json,subprocess,sys
from fractions import Fraction
def triple(value):
    whole=value.numerator//value.denominator
    return [whole,(value-whole).numerator,(value-whole).denominator]
cases=[]
for origin in [Fraction(0),Fraction(4096)+Fraction(1,7),Fraction(17)+Fraction(2,21)]:
    for span in [Fraction(872,3),Fraction(8192)+Fraction(1,7),Fraction(20000)+Fraction(1,7),Fraction(1234567,4099)]:
        for count in [int(span)+1,int(span)+257]:
            cases.append(dict(origin=triple(origin),span=triple(span),frames=count,offsets=[0,1,37,count//2,count-1,count]))
result=subprocess.run([sys.argv[1],'--oracle'],input=json.dumps(cases),capture_output=True,text=True,timeout=15)
assert result.returncode==0,result.stderr
rows=json.loads(result.stdout);assert len(rows)==len(cases)
for case,row in zip(cases,rows):
    def value(t):return Fraction(t[0])+Fraction(t[1],t[2])
    origin=value(case['origin']);span=value(case['span']);step=span/case['frames']
    assert Fraction(*row['step'])==step
    assert [value(p) for p in row['positions']]==[origin+offset*step for offset in case['offsets']]
print(f'processing_grid_cases={len(cases)} independent_fraction_coordinates={len(cases)*6}')
