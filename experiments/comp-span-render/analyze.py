#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent exact-grid/PCM/spectral observations, not sound-quality admission."""
from pathlib import Path
from fractions import Fraction
import hashlib,json,sys
import numpy as np
root=Path(sys.argv[1]);report=json.loads((root/'report.json').read_text());rows=[];maximum=0;off_peak=0
position=lambda p:Fraction(p[0])+Fraction(p[1],p[2])
for r in report['rows']:
    data=(root/r['pcm']).read_bytes();v=np.frombuffer(data,dtype='<f4').reshape(-1,2)
    assert len(v)==r['outputFrames'] and np.isfinite(v).all()
    assert float(np.max(np.abs(v.astype(float))))==r['peak']
    step=Fraction(*r['processingStep']);assert step*r['processingFrames']==position(r['rawSpan'])
    assert abs(r['backendPitchFactor']-2**(r['pitchMilliCents']/1200000)/float(step))<1e-14
    window=v[len(v)//4:3*len(v)//4].astype(float);n=1
    while n<len(window)*8:n*=2
    frequencies=np.fft.rfftfreq(n,1/r['rate']);observed=[]
    for ch in range(2):
        wanted=(997 if ch==0 else 1703)*2**(r['pitchMilliCents']/1200000)
        spectrum=np.abs(np.fft.rfft(window[:,ch]*np.hanning(len(window)),n));near=np.where((frequencies>=wanted*.9)&(frequencies<=wanted*1.1))[0];all_bins=np.where((frequencies>=wanted*.25)&(frequencies<=min(wanted*4,r['rate']*.49)))[0]
        local=near[np.argmax(spectrum[near])];global_peak=all_bins[np.argmax(spectrum[all_bins])];actual=float(frequencies[local]);relative=float(spectrum[local]/spectrum[global_peak])
        assert actual==r['frequency'][ch]['measured'] and abs(relative-r['frequency'][ch]['fundamentalRelativeAmplitude'])<1e-9
        error=abs(actual/wanted-1);maximum=max(maximum,error);off_peak+=abs(float(frequencies[global_peak])/wanted-1)>=.01
        observed.append(dict(wanted=wanted,fundamentalHz=actual,relativeFrequencyError=error,globalPeakHz=float(frequencies[global_peak]),fundamentalRelativeAmplitude=relative))
    rows.append(dict(pcm=r['pcm'],sha256=hashlib.sha256(data).hexdigest(),frames=len(v),observations=observed))
assert maximum==report['maxRelativeFrequencyError'] and maximum<report['frequencyGuard']==.01
result=dict(format='sc-comp-span-analysis-v1',actualR3Renders=report['actualR3Renders'],maxRelativeFrequencyError=maximum,dominantPeaksOutsideFundamentalGuard=off_peak,rows=rows,formantsQualified=False,phaseQualified=False,qualityAdmission=False)
(root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='rows'}))
