#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded actual R3/R2 multi-anchor runs; owned signals only, no audio endpoint."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys
import time
import numpy as np

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('probe',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
probe=args.probe.resolve();root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
if sys.platform=='linux':
    import resource
    resource.setrlimit(resource.RLIMIT_AS,(512*1024*1024,512*1024*1024))
started=time.monotonic();records=[]
def save(name,value):
    (root/name).write_text(json.dumps(value,indent=2)+'\n')
def decode(path):
    data=path.read_bytes();assert len(data)<2*1024*1024 and data[:4]==b'RIFF' and data[8:16]==b'WAVEfmt '
    assert struct.unpack_from('<I',data,4)[0]==len(data)-8 and struct.unpack_from('<I',data,16)[0]==16
    encoding,channels,rate,byte_rate,align,bits=struct.unpack_from('<HHIIHH',data,20)
    assert encoding==3 and channels in (2,8) and rate==48000 and byte_rate==rate*channels*4 and align==channels*4 and bits==32
    assert data[36:40]==b'data' and struct.unpack_from('<I',data,40)[0]==len(data)-44
    values=np.frombuffer(data[44:],dtype='<f4').reshape(-1,channels);assert np.isfinite(values).all()
    return values,hashlib.sha256(data).hexdigest()

cases=[]
for family,channels in [('impulse',2),('attack',2),('sustain',2),('attack',8)]:
    for finer in [False,True]:
        for profile in ['constant','uniform-map','nonuniform-map']:
            for block in [97,512]:
                cases.append({'family':family,'channels':channels,'finer':finer,'profile':profile,'block':block,'pitchMilliCents':0,'together':True})
# Explicit apart versus together on the same grouped map, and independent pitch.
for finer in [False,True]:
    cases.append({'family':'attack','channels':8,'finer':finer,'profile':'nonuniform-map','block':512,'pitchMilliCents':0,'together':False})
    cases.append({'family':'attack','channels':2,'finer':finer,'profile':'nonuniform-map','block':512,'pitchMilliCents':700007,'together':True})
assert len(cases)==52
try:
    for index,case in enumerate(cases):
        assert time.monotonic()-started<60,'Global experiment deadline'
        folder=root/f'case-{index:02d}'
        process=subprocess.Popen([str(probe),json.dumps(case,separators=(',',':')),str(folder)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        begin=time.monotonic()
        try:
            out,err=process.communicate(timeout=min(10,60-(time.monotonic()-started)))
        except subprocess.TimeoutExpired:
            process.kill();out,err=process.communicate()
            records.append({'id':index,'request':case,'pid':process.pid,'exitCode':process.returncode,'timeout':True,'stdout':out,'stderr':err})
            save('processes.json',records);raise
        record={'id':index,'request':case,'pid':process.pid,'exitCode':process.returncode,'seconds':time.monotonic()-begin,'stdout':out,'stderr':err}
        records.append(record);save('processes.json',records)
        assert process.returncode==0 and len(out)<=32768 and len(err)<=32768,'Candidate process failed or exceeded text bank'
        result=json.loads(out);assert result==json.loads((folder/'render.json').read_text()) and result['request']==case
        source,source_hash=decode(folder/'source.wav');rendered,render_hash=decode(folder/'rendered.wav')
        assert source.shape==(16384,case['channels']) and len(rendered)==result['writtenFrames'] and rendered.shape[1]==case['channels']
        record['sourceSha256']=source_hash;record['renderSha256']=render_hash;record['result']=result
        record['warnings']=len(err.splitlines());save('processes.json',records)
        print(json.dumps({'case':index,'engine':'R3' if case['finer'] else 'R2','family':case['family'],'channels':case['channels'],'profile':case['profile'],'block':case['block'],'writtenFrames':len(rendered),'exactDrain':result['exactDrain'],'warningLines':record['warnings']}),flush=True)
    assert time.monotonic()-started<60,'Global experiment deadline after terminal run'
    summary={'format':'sc-warp-candidate-run-v1','sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
             'sourceWasUncommitted':bool(subprocess.check_output(['git','status','--porcelain'],text=True).strip()),
             'actualProcesses':len(records),'terminalSuccesses':sum(p['exitCode']==0 for p in records),'exactDrains':sum(p['result']['exactDrain'] for p in records),
             'seconds':time.monotonic()-started,'globalDeadlineSeconds':60,'perProcessDeadlineSeconds':10,'addressSpaceCeilingBytes':512*1024*1024,
             'probeSha256':hashlib.sha256(probe.read_bytes()).hexdigest(),'runnerSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
             'numpyVersion':np.__version__,'nativePlatform':sys.platform,'nativeAudio':False,'shippingProcessorChanged':False,'QStretchPassed':False,'groupPhaseQualified':False}
    save('summary.json',summary);print(json.dumps(summary))
except BaseException as error:
    save('failure.json',{'error':repr(error),'terminalProcesses':len(records),'seconds':time.monotonic()-started,'nativeAudio':False})
    raise
