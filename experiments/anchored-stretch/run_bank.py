#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run exactly the preregistered synthetic candidate bank, retaining all outcomes."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,resource,subprocess,time
os.environ['OPENBLAS_NUM_THREADS']='1';os.environ['OMP_NUM_THREADS']='1'
p=argparse.ArgumentParser();p.add_argument('probe',type=Path);p.add_argument('output',type=Path);p.add_argument('--build-receipt',type=Path,required=True);args=p.parse_args()
root=args.output.resolve();root.mkdir(parents=True,exist_ok=False);probe=args.probe.resolve();source=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
contract=json.loads((source/'contract.json').read_text());(root/'contract.json').write_bytes((source/'contract.json').read_bytes())
receipt=json.loads(args.build_receipt.read_text());assert receipt['exitCode']==0 and receipt['unchangedSourcesAndLibrary'] and receipt['probeSha256']==sha(probe)
(root/'build-receipt.json').write_bytes(args.build_receipt.read_bytes())
started=time.monotonic();records=[]
def limits():resource.setrlimit(resource.RLIMIT_AS,(512*1024*1024,512*1024*1024))
for family in ['impulse','attack','sustain','cancellation']:
 for profile in ['identity','uniform','nonuniform']:
  for block in [97,512]:
   request={'family':family,'profile':profile,'block':block};index=len(records);deadline=min(10,60-(time.monotonic()-started));assert deadline>0
   directory=root/f'case-{index:02d}';childStart=time.monotonic();child=subprocess.Popen([str(probe),json.dumps(request,separators=(',',':')),str(directory)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,preexec_fn=limits)
   timeout=False
   try:out,err=child.communicate(timeout=deadline)
   except subprocess.TimeoutExpired:timeout=True;child.kill();out,err=child.communicate()
   row={'id':index,'request':request,'pid':child.pid,'exitCode':child.returncode,'timeout':timeout,'seconds':time.monotonic()-childStart,'stdout':out,'stderr':err,'waves':{}}
   for name in ['source','generated','rendered']:
    path=directory/(name+'.wav')
    if path.exists():row['waves'][name]={'sha256':sha(path),'bytes':path.stat().st_size}
   if child.returncode==0:row['result']=json.loads(out)
   records.append(row);(root/'processes.json').write_text(json.dumps(records,indent=2)+'\n')
   assert not timeout and child.returncode==0 and err=='',row
summary={'format':'sc-anchored-stretch-bank-v1','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'actualProcesses':len(records),'terminalSuccesses':sum(r['exitCode']==0 for r in records),'seconds':time.monotonic()-started,'globalDeadlineSeconds':60,'childDeadlineSeconds':10,'addressSpaceCeilingBytes':512*1024*1024,'probeSha256':sha(probe),'runnerSha256':sha(Path(__file__)),'contractSha256':sha(source/'contract.json'),'analysisSourceSha256BeforeRun':sha(source/'analyze_bank.py'),'rendererSourceSha256':sha(source/'render_probe.cpp'),'buildReceiptSha256':sha(args.build_receipt),'sourceCommit':receipt['preflight']['sourceCommit'],'sourceBase':contract['sourceBase'],'sourceWasUncommitted':True,'nativeAudio':False,'shippingAdopted':False,'fullQualityQualified':False}
(root/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary))
