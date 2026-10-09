#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent Python big-integer oracle invokes the actual C++ timing code."""
import json
import math
import random
import subprocess
import sys

maximum=(1<<63)-1
rates=[8000,11025,44100,48000,96000,192000,384000]
rng=random.Random(20261009)
cases=[]
for source in rates:
    for project in rates:
        for offset in [0,1,(1<<53)+17,maximum]:
            cases.append((source,project,offset,17))
for _ in range(128):
    cases.append((rng.randint(8000,384000),rng.randint(8000,384000),rng.randint(0,maximum),rng.randint(0,100000)))
checks=0
for source,project,offset,origin in cases:
    divisor=math.gcd(source,project)
    numerator=source//divisor;denominator=project//divisor
    quotient,remainder=divmod(offset*numerator,denominator)
    expected={'frame':origin+quotient,'fraction':remainder,'denominator':denominator}
    result=subprocess.run([sys.argv[1],'--timing-probe',str(source),str(project),str(offset),str(origin)],
                          capture_output=True,text=True,timeout=5)
    if expected['frame']>maximum:
        assert result.returncode!=0,(source,project,offset,origin,result.stdout)
    else:
        assert result.returncode==0,result.stderr
        assert json.loads(result.stdout)==expected,(source,project,offset,origin,result.stdout,expected)
    checks+=1
print(json.dumps({'checks':checks,'oracle':'independent Python arbitrary precision integers',
                  'actualCppExecuted':True,'sourceProjectMappingOnly':True,
                  'sessionConversion':False,'nativeAudio':False}))
