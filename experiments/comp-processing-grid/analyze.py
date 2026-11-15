#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent windowed spectra of owned PCM; NumPy is an analysis dependency."""
from pathlib import Path
import hashlib,json,sys
import numpy as np
root=Path(sys.argv[1]);report=json.loads((root/'report.json').read_text())
rows=[];maximum=0
for render in report['rows']:
    path=root/render['pcm'];data=np.frombuffer(path.read_bytes(),dtype='<f4').reshape(-1,2)
    assert len(data)==render['outputFrames'] and np.isfinite(data).all()
    middle=data[len(data)//4:len(data)*3//4]
    for observed in render['frequencies']:
        channel=observed['channel'];wanted=observed['requestedHz']
        expected=wanted if render['compensated'] else wanted*render['step'][0]/render['step'][1]
        count=262144
        power=np.abs(np.fft.rfft(middle[:,channel]*np.hanning(len(middle)),n=count))
        low=int(expected*.9*count/48000);high=int(expected*1.1*count/48000)
        peak=low+int(np.argmax(power[low:high]));log=np.log(np.maximum(power[peak-1:peak+2],1e-30))
        offset=.5*(log[0]-log[2])/(log[0]-2*log[1]+log[2])
        frequency=(peak+offset)*48000/count;error=abs(frequency/expected-1)
        if render['compensated']:maximum=max(maximum,error)
        rows.append(dict(pcm=render['pcm'],channel=channel,compensated=render['compensated'],expectedHz=expected,
                         observedSpectralHz=frequency,relativeError=error,pcmSha256=hashlib.sha256(path.read_bytes()).hexdigest()))
assert maximum<.005,maximum
nominal=np.frombuffer((root/'nominal.pcm').read_bytes(),dtype='<f4').reshape(-1,2)
expanded=np.frombuffer((root/'expanded.pcm').read_bytes(),dtype='<f4').reshape(-1,2)
comparison=report['nominalComparison'];prefix=comparison['prefixOutputFrames']
inner=expanded[prefix:prefix+len(nominal)]
assert len(nominal)==comparison['nominalOutputFrames'] and len(expanded)==len(nominal)+prefix+comparison['suffixOutputFrames']
changed=int(np.count_nonzero(nominal!=inner));difference=float(np.max(np.abs(nominal.astype(float)-inner)))
assert changed==comparison['changedSamples'] and difference==comparison['maxSampleDifference'] and changed>0
result=dict(format='sc-comp-grid-spectral-analysis-v1',numpyVersion=np.__version__,fftFrames=262144,
            window='Hann on middle half',rows=rows,compensatedMaxRelativeSpectralError=maximum,
            nominalChangedSamples=changed,nominalMaxSampleDifference=difference,fullProcessingQuality=False)
(root/'spectral-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='rows'}))
