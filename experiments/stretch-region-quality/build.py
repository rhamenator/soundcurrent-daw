#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the Linux-only diagnostic against an explicitly qualified local cache.
No application/vendor rebuild, installation, VM or audio endpoint is requested.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,time,zipfile,re
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
build=args.build.resolve();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
# This cache was compiled for these exact production files. Timestamp-only Git
# checkouts cannot replace a source-content assertion or qualify a changed ABI.
with zipfile.ZipFile(root/'tests/results/M2/2026-10-10-explicit-stretch-context/capture.zip') as z:inputs=json.loads(z.read('source-inputs.json'))['inputs']
checked={name:value for name,value in inputs.items() if name.startswith(('include/','src/','third_party/'))}
assert checked and all(sha(root/name)==value for name,value in checked.items()),'Production/library inputs differ from the qualified cache'
libs=['sc-media','sc-stretch-render-protocol','sc-media','sc-project-store','sc-project-import-state','sc-inspection-bundle','sc-media-provenance','sc-wave-report','sc-import-report','sc-foreign-snapshot','sc-reaper-structure','sc-playback','sc-audio-bridge','sc-capture','sc-engine','sc-positioned-resampling','sc-wave-validation','sc-approved-media','sc-session']
paths=[build/('lib'+name+'.a') for name in libs];assert all(path.is_file() for path in paths)
cache=(build/'CMakeCache.txt').read_text()
sndfile=Path(re.search(r'^SNDFILE_LIBRARY:[^=]+=([^\n]+)$',cache,re.M)[1]);assert sndfile.is_file()
source=Path(__file__).with_name('probe.cpp');audit=root/'tests/rt_audit.cpp';exe=output/'sc-region-quality-probe'
command=['c++','-std=c++20','-O2','-DNDEBUG','-fno-fast-math','-DSC_WRAP_LIBC','-Wall','-Wextra','-Wpedantic',
         '-I'+str(root/'include'),'-I'+str(root/'third_party'),'-I'+str(root/'tests'),str(source),str(audit),'-o',str(exe),
         '-Wl,--start-group',*map(str,paths),'-Wl,--end-group',str(sndfile),'-lcrypto','-pthread',
         '-Wl,--wrap=malloc','-Wl,--wrap=free','-Wl,--wrap=calloc','-Wl,--wrap=realloc','-Wl,--wrap=pthread_mutex_lock']
started=time.monotonic();run=subprocess.run(command,capture_output=True,text=True,timeout=120)
(output/'build.log').write_text(run.stdout+run.stderr)
result={'format':'sc-region-quality-build-v1','command':command,'exitCode':run.returncode,'seconds':time.monotonic()-started,
        'sourceCommit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        'sourceWasUncommitted':bool(subprocess.check_output(['git','status','--porcelain'],cwd=root)),
        'compiler':subprocess.check_output(['c++','--version'],text=True).splitlines()[0],
        'productionInputHashes':checked,'librarySha256':{str(path):sha(path) for path in paths},
        'sndfilePath':str(sndfile),'sndfileSha256':sha(sndfile),'sourceSha256':sha(source),'auditSha256':sha(audit),'buildScriptSha256':sha(Path(__file__)),
        'exeSha256':sha(exe) if run.returncode==0 else None,'nativeWindowsBuilt':False,'applicationRebuilt':False}
(output/'build.json').write_text(json.dumps(result,indent=2)+'\n');assert run.returncode==0,run.stderr
print(json.dumps({'exitCode':run.returncode,'exe':str(exe),'sha256':result['exeSha256'],'seconds':result['seconds'],'productionInputsChecked':len(checked)}))
