#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the C++ mapping against independent arbitrary-precision rationals."""
import json
import math
import random
import subprocess
import sys
from fractions import Fraction

MAX_FRAME = (1 << 63) - 1
MAX_DENOMINATOR = (1 << 64) - 1
rates = [8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200,
         96000, 192000, 384000]
cases = []
for source in rates:
    for project in rates:
        for fraction, denominator in [(0, 1), (1, 7), (6, 7),
                                      (MAX_DENOMINATOR - 1, MAX_DENOMINATOR)]:
            cases.append((source, project, 4096, fraction, denominator, 113, -37))

# Large positions, signed-limit translations, exact carries, and independently
# generated rates/fractions. Neither floating point nor the C++ decomposition
# is used to compute the expected source position.
cases += [
    (48000, 48000, MAX_FRAME, 1, 2, 0, 0),
    (48000, 48000, MAX_FRAME, 1, 2, 1, 0),
    (8000, 384000, MAX_FRAME, 1, 7, 17, -(1 << 63)),
    (384000, 8000, MAX_FRAME, 0, 1, 0, -(1 << 63)),
    (48000, 44100, (1 << 53) + 17, 1, 11, 10001, -10000),
    (48000, 48000, 0, 1, 2, 1, -1),  # Translation must itself be valid.
    (44100, 48000, 1, 79, 80, 0, -2),
    (44100, 48000, 0, 1, 80, 0, -1),
    (48000, 48000, 1, 1, 2, -1, 0),
    (48000, 48000, -1, 0, 1, 0, 0),
    (48000, 48000, 0, -1, 7, 0, 0),
    (48000, 48000, 0, 7, 7, 0, 0),
    (48000, 48000, 0, 0, 0, 0, 0),
    (7999, 48000, 0, 0, 1, 0, 0),
    (48000, 384001, 0, 0, 1, 0, 0),
]
rng = random.Random(20261009)
for _ in range(256):
    denominator = rng.choice([7, 11, 997, (1 << 53) + 9, MAX_DENOMINATOR])
    cases.append((rng.randint(8000, 384000), rng.randint(8000, 384000),
                  rng.choice([4096, (1 << 53) + 17, MAX_FRAME - 100000]),
                  rng.randrange(denominator), denominator,
                  rng.choice([0, 113, (1 << 53) + 17, MAX_FRAME]),
                  rng.choice([-37, 0, 113, -(1 << 63)])))

if len(sys.argv)==3 and sys.argv[2]=='--playback-rate':
    cases=[]
    for source in [8000,44100,48000,384000]:
        for project in [8000,44100,48000,384000]:
            for speed in [(1,4),(1,2),(1333,1000),(4,1),(3999,4000)]:
                for fraction,denominator in [(0,1),(1,7),(MAX_DENOMINATOR-1,MAX_DENOMINATOR)]:
                    cases.append((source,project,4096,fraction,denominator,113,-7,*speed))
    for _ in range(100):
        n=rng.randint(1,4000);d=rng.randint(1,4000)
        cases.append((rng.randint(8000,384000),rng.randint(8000,384000),
                      rng.choice([4096,(1<<53)+17,MAX_FRAME-100000]),1,997,
                      rng.choice([0,113,MAX_FRAME]),rng.choice([-37,0,113,-(1<<63)]),n,d))
    for speed in [(0,1),(1,0),(4001,4000),(1,5),(5,1),(-1,1),(1,4294967295)]:
        cases.append((48000,44100,4096,1,7,113,-37,*speed))
    cases += [(384000,8000,MAX_FRAME,0,1,MAX_FRAME,0,4,1),
              (8000,384000,MAX_FRAME,1,7,0,-(1<<63),1,4)]

accepted = refused = 0
for case in cases:
    source, project, frame, numerator, denominator, offset, translation = case[:7]
    rate_n,rate_d=case[7:] if len(case)==9 else (1,1)
    valid = (8000 <= source <= 384000 and 8000 <= project <= 384000
             and 0 <= frame <= MAX_FRAME and 0 < denominator <= MAX_DENOMINATOR
             and 0 <= numerator < denominator and offset >= 0
             and 0<rate_n<=4000 and 0<rate_d<=4000
             and rate_n*4>=rate_d and rate_n<=rate_d*4)
    if valid:
        origin = frame + Fraction(numerator, denominator)
        step = Fraction(source, project)*Fraction(rate_n,rate_d)
        common = math.lcm(Fraction(numerator, denominator).denominator,
                          step.denominator)
        translated = origin + translation * step
        expected = translated + offset * step
        valid = (common <= MAX_DENOMINATOR and translated >= 0
                 and translated.numerator // translated.denominator <= MAX_FRAME
                 and expected.numerator // expected.denominator <= MAX_FRAME)
    result = subprocess.run([sys.argv[1], '--fraction-probe', *map(str, case)],
                            capture_output=True, text=True, timeout=5)
    if not valid:
        assert result.returncode != 0, (case, result.stdout)
        refused += 1
        continue
    assert result.returncode == 0, (case, result.stderr)
    actual = json.loads(result.stdout)
    assert 0 <= actual['fraction'] < actual['denominator'] <= MAX_DENOMINATOR
    assert actual['denominator'] == common, (case, actual, common)
    position = actual['frame'] + Fraction(actual['fraction'], actual['denominator'])
    assert position == expected, (case, actual, expected)
    accepted += 1
print(json.dumps({'checks': len(cases), 'accepted': accepted, 'refused': refused,
                  'oracle': 'independent Python arbitrary-precision Fraction',
                  'actualCppExecuted': True, 'nativeAudio': False}))
