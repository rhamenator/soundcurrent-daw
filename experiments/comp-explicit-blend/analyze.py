#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent scoped curve/PCM and artifact observations; no quality verdict."""
from pathlib import Path
import json,sys
import numpy as np
bank=Path(sys.argv[1]);root=Path(sys.argv[2]);source=json.loads((bank/'report.json').read_text());report=json.loads((root/'report.json').read_text());rows=[]
for row in report['rows']:
    case=next(c for c in source['cases'] if c['case']==row['case']);frames=row['currentFrames'];edge=row['edgeFrames'];width=row['width']
    def read(p):return np.frombuffer(p.read_bytes(),dtype='<f4').reshape(-1,2)
    old=read(bank/row['case']/'nominal.pcm');prefix=read(bank/row['case']/'prefix-context.pcm');suffix=read(bank/row['case']/'suffix-context.pcm');hard=read(bank/row['case']/'assembled.pcm');actual=read(root/row['pcm'])
    assert len(actual)==row['outputFrames']==frames+2*edge and np.isfinite(actual).all()
    assert row['prefixPermission']==[0,width] and row['suffixPermission']==[frames-width,frames]
    before=case['prefixCropFrame'];candidate=np.zeros_like(actual)
    candidate[:edge+width]=prefix[before:before+edge+width]
    candidate[edge+frames-width:]=suffix[frames-width:frames+edge]
    expected=np.concatenate([candidate[:edge],old,candidate[-edge:]]).astype(float)
    t=np.arange(1,width+1,dtype=float)/(width+1);curve=10*t**3-15*t**4+6*t**5
    expected[edge:edge+width]=curve[:,None]*old[:width]+(1-curve[:,None])*candidate[edge:edge+width]
    expected[edge+frames-width:edge+frames]=(1-curve[:,None])*old[-width:]+curve[:,None]*candidate[edge+frames-width:edge+frames]
    error=float(np.max(np.abs(actual.astype(float)-expected)))
    assert error<1e-6,error
    assert actual[edge+width:edge+frames-width].tobytes()==old[width:frames-width].tobytes()
    assert actual[:edge].tobytes()==candidate[:edge].tobytes() and actual[-edge:].tobytes()==candidate[-edge:].tobytes()
    measurements={}
    for at,key in [(edge,'prefixJoin'),(edge+frames,'suffixJoin')]:
        v=actual.astype(float);jump=v[at]-v[at-1];beforeStep=v[at-1]-v[at-2];afterStep=v[at+1]-v[at]
        step=float(np.max(np.abs(jump)));deviation=float(max(np.max(np.abs(jump-beforeStep)),np.max(np.abs(jump-afterStep))))
        assert step==row[key]['stepPeak'] and deviation==row[key]['slopeDeviationPeak']
        # Same observed window on hard/blended joins; HF energy is only a proxy.
        low=max(0,at-width);high=min(len(actual),at+width);window=np.hanning(high-low)[:,None]
        def energy(pcm):
            spectrum=np.fft.rfft(pcm[low:high]*window,n=4096,axis=0)
            return float(np.sum(np.abs(spectrum[np.fft.rfftfreq(4096,1/48000)>=8000])**2))
        hardEnergy=energy(hard);blendEnergy=energy(actual)
        measurements[key]=dict(stepPeak=step,slopeDeviationPeak=deviation,hardHighFrequencyEnergy=hardEnergy,
                               blendedHighFrequencyEnergy=blendEnergy,highFrequencyEnergyRatio=blendEnergy/hardEnergy if hardEnergy else None)
    # Cancellation/artifact tradeoff within each permitted region remains visible.
    rms=[]
    for first,last in [(edge,edge+width),(edge+frames-width,edge+frames)]:
        reference=float(np.sqrt(np.mean(hard[first:last].astype(float)**2)));observed=float(np.sqrt(np.mean(actual[first:last].astype(float)**2)))
        rms.append(observed/reference if reference else None)
    rows.append(dict(case=row['case'],width=width,wholePcmCurveError=error,permissionScopeBitPreserved=True,windowRmsRatios=rms,**measurements))
result=dict(format='sc-explicit-comp-blend-analysis-v1',numpyVersion=np.__version__,rows=rows,assemblies=len(rows),
            maximumCurveError=max(r['wholePcmCurveError'] for r in rows),maxJoinStep=max(r[k]['stepPeak'] for r in rows for k in ['prefixJoin','suffixJoin']),
            maxSlopeDeviation=max(r[k]['slopeDeviationPeak'] for r in rows for k in ['prefixJoin','suffixJoin']),fullProcessingQuality=False,modelVerifierChanged=False)
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='rows'}))
