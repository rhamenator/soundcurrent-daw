#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent whole-PCM, exact recipe and boundary checks for retained edges."""
from pathlib import Path
from fractions import Fraction
import hashlib,json,sys
import numpy as np
root=Path(sys.argv[1]);report=json.loads((root/'report.json').read_text());rows=[]
def value(p):return Fraction(p[0])+Fraction(p[1],p[2])
for case in report['cases']:
    path=root/case['case'];channels=case['channels'];edge=case['edgeFrames'];frames=case['nominalOutputFrames']
    def read(name):return np.frombuffer((path/name).read_bytes(),dtype='<f4').reshape(-1,channels)
    current=read('nominal.pcm');prefix=read('prefix-context.pcm');suffix=read('suffix-context.pcm');actual=read('assembled.pcm')
    assert len(current)==frames and len(prefix)==len(suffix)==case['contextOutputFrames']
    assert len(actual)==frames+edge*2==case['writtenFrames'] and np.isfinite(actual).all()
    assert actual[edge:edge+frames].tobytes()==current.tobytes()
    first=case['prefixCropFrame'];last=case['suffixCropFrame']
    assert actual[:edge].tobytes()==prefix[first:first+edge].tobytes() and actual[-edge:].tobytes()==suffix[last:last+edge].tobytes()
    assert float(np.max(np.abs(actual.astype(float))))==case['floatPeak']
    ratio=Fraction(case['timeDenominator'],case['timeNumerator']);step=value(case['contextRawSpan'])/case['processingFrames']
    assert step==Fraction(*case['processingStep'])
    assert value(case['prefixRawOrigin'])+first*ratio==value(case['nominalRawOrigin'])-edge*ratio
    assert value(case['suffixRawOrigin'])+last*ratio==value(case['nominalRawOrigin'])+case['nominalRawFrames']
    assert case['contextOutputFrames']*ratio==value(case['contextRawSpan'])
    measured={}
    for at,key in [(edge,'prefixJoin'),(edge+frames,'suffixJoin')]:
        f=actual.astype(float);jump=f[at]-f[at-1];before=f[at-1]-f[at-2];after=f[at+1]-f[at]
        peak=float(np.max(np.abs(jump)));deviation=float(max(np.max(np.abs(jump-before)),np.max(np.abs(jump-after))))
        assert peak==case[key]['stepPeak'] and deviation==case[key]['slopeDeviationPeak']
        measured[key]=dict(stepPeak=peak,slopeDeviationPeak=deviation)
    rows.append(dict(case=case['case'],**measured,assembledSha256=hashlib.sha256((path/'assembled.pcm').read_bytes()).hexdigest()))
result=dict(format='sc-comp-edge-assembly-analysis-v1',numpyVersion=np.__version__,cases=len(rows),rows=rows,
            maxJoinStep=max(row[key]['stepPeak'] for row in rows for key in ['prefixJoin','suffixJoin']),
            maxSlopeDeviation=max(row[key]['slopeDeviationPeak'] for row in rows for key in ['prefixJoin','suffixJoin']),
            innerPcmBitPreserved=True,fullQualityQualified=False)
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='rows'}))
