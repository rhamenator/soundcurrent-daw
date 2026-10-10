#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the isolated candidate with source/library hashes before and after compilation."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,datetime,time
p=argparse.ArgumentParser();p.add_argument('candidateSources',type=Path);p.add_argument('sessionLibrary',type=Path);p.add_argument('output',type=Path);args=p.parse_args();root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
repo=Path(__file__).resolve().parents[2];candidate=args.candidateSources.resolve();library=args.sessionLibrary.resolve();compiler=Path(shutil.which('c++')).resolve();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[repo/'experiments/anchored-stretch/render_probe.cpp',repo/'experiments/warp-map/warp_map.cpp',repo/'experiments/warp-map/warp_map.hpp',library,compiler]
for prefix in ['include','src']:
 files+=sorted(p for p in (repo/prefix).rglob('*') if p.is_file())
files+=sorted(p for prefix in ['signalsmith-stretch','linear'] for p in (candidate/prefix).rglob('*') if p.is_file() and p.suffix in ['.h','.txt'])
files+=[repo/'third_party/nlohmann/json.hpp']
files=list(dict.fromkeys(files));before={str(p):sha(p) for p in files};exe=root/'sc-anchored-stretch-probe'
command=[str(compiler),'-std=c++20','-O2','-DNDEBUG','-fno-fast-math','-Wall','-Wextra','-Wpedantic','-I'+str(repo/'include'),'-I'+str(repo/'third_party'),'-I'+str(candidate/'signalsmith-stretch'),'-I'+str(candidate/'linear/include'),str(repo/'experiments/anchored-stretch/render_probe.cpp'),str(repo/'experiments/warp-map/warp_map.cpp'),str(library),'-pthread','-o',str(exe)]
pre={'format':'sc-anchored-stretch-build-preflight-v1','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'command':command,'files':before,'sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),'workingTreeStatus':subprocess.check_output(['git','status','--porcelain'],cwd=repo,text=True)};(root/'preflight.json').write_text(json.dumps(pre,indent=2)+'\n')
start=time.monotonic();child=subprocess.Popen(command,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
try:out,err=child.communicate(timeout=60)
except subprocess.TimeoutExpired:child.kill();out,err=child.communicate();raise
(root/'stdout.log').write_text(out);(root/'stderr.log').write_text(err);after={str(p):sha(p) for p in files};record={'format':'sc-anchored-stretch-build-receipt-v1','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pid':child.pid,'exitCode':child.returncode,'seconds':time.monotonic()-start,'preflightSha256':sha(root/'preflight.json'),'preflight':pre,'after':after,'unchangedSourcesAndLibrary':before==after,'probeSha256':sha(exe) if exe.exists() else None};(root/'receipt.json').write_text(json.dumps(record,indent=2)+'\n');assert child.returncode==0 and before==after,record;print(json.dumps({'pid':child.pid,'exitCode':child.returncode,'seconds':record['seconds'],'sourceFiles':len(files),'probeSha256':record['probeSha256']}))
