#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain source-bound real repeated desktop render/review/Apply acceptance."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for name in ['build','evidence','public']:p.add_argument(name,type=Path)
a=p.parse_args();build=a.build.resolve();base=a.evidence.resolve();public=a.public.resolve()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not git('status','--porcelain');head=git('rev-parse','HEAD')
files=[n for n in git('ls-files').splitlines() if (n.startswith(('include/','src/','ui/','third_party/','tests/','tools/','.github/')) and not n.startswith('tests/results/')) or n in ['CMakeLists.txt','research/linux-ci-test-durations.json']]
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-session-tests','sc-comp-span-v2-fixture','sc-comp-assembly-ui-tests','sc-comp-transition-ui-tests','sc-comp-assembly-job-tests','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker']]+sorted(build.glob('lib*.a'))
binaries={str(p):sha(p) for p in inputs};base.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=base))
roots=[build/n for n in ['comp-repeat-ui-evidence','comp-transition-ui-evidence','comp-span-v2-evidence','comp-assembly-ui-evidence']]
for r in roots:r.mkdir(exist_ok=True)
before=[set(r.iterdir()) for r in roots]
names=['ci-shard-coverage','session-state','desktop-comp-repeated-transition','desktop-comp-transition-create','comp-clocked-span-worker','desktop-comp-assembly-apply']
pattern='^('+'|'.join(names)+')$';command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','--parallel','1','-R',pattern]
selected=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1','-R',pattern],text=True));assert sorted(t['name'] for t in selected['tests'])==sorted(names)
receipt=dict(sourceHead=head,sourceTree=git('rev-parse','HEAD^{tree}'),sourceInputs=sources,binaryInputs=binaries,command=command)
(scope/'started.json').write_text(json.dumps(receipt,indent=2)+'\n');start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
 proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait()
receipt.update(exitCode=code,seconds=time.monotonic()-start,pid=proc.pid)
(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes())
(scope/'result.json').write_text(json.dumps(receipt,indent=2)+'\n');assert code==0,(scope,code)
runs=[];reports=[]
for root,old in zip(roots,before):
 new=set(root.iterdir())-old;assert len(new)==1;run=new.pop();runs.append(run);reports.append(json.loads((run/'report.json').read_text()))
repeat,creation,v2,apply=reports
assert repeat['actualRepeatedMainWindow'] and repeat['actualAdoptions']==9 and repeat['successfulAdoptionWorkers']==15 and repeat['mixedRetained']==3
assert all(p['exitCode']==0 for p in repeat['controllerProcesses'])
cases=[c for r in repeat['reports'] for c in r['cases']];assert len(cases)==8
assert all(c['assemblyCreditsReleased'] and c['asynchronousClose'] for c in cases)
steps=[g for c in cases if c['case']==0 for g in c['generations']];assert len(steps)==9
assert all(s['undoRedoSaveReopen'] and s['noAudioBeforeApply'] and s['unchangedSamples']>0 and s['liveExportSamples']>0 for s in steps)
assert v2['applicationEntryEnabled'] and v2['successfulV2Workers']==9 and v2['refusedV2Workers']==4 and v2['projectBytesUnchanged']
assert all(not row['retirementPending'] and not row['observationTimedOut'] for row in v2['processes'] if row['kind']=='v2-worker')
assert len([c for r in creation['reports'] for c in r['cases']])==8
legacy=[c['exportFileSha256'] for r in apply['reports'] for c in r['cases'] if c['case']==0]
with zipfile.ZipFile(repo/'tests/results/M2/2026-10-14-comp-history-sharing/acceptance.zip') as z:previous=json.loads(z.read('qualification.json'))
assert legacy==[c['exportFileSha256'] for r in previous['reports'][2]['reports'] for c in r['cases'] if c['case']==0]
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(p):sha(p) for p in inputs} and head==git('rev-parse','HEAD')
receipt.update(reports=reports,runs=[str(r) for r in runs],sourceAndBinariesUnchanged=True,actualRepeatedDesktopAdoptions=9,mixedRetainedCases=3,legacyExportHashesUnchanged=True,unchangedSamples=sum(s['unchangedSamples'] for s in steps),liveExportSamples=sum(s['liveExportSamples'] for s in steps),physicalAudio=False,qualityAdmission=False,nativeWindowsQualified=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
manifest={};blobs=set()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 z.write(scope/'qualification.json','qualification.json')
 for root,prefix in [(scope,'logs/')]+list(zip(runs,['repeated-desktop/','first-generation/','v2-workers/','apply-regression/'])):
  for f in sorted(root.rglob('*')):
   if not f.is_file() or f==scope/'qualification.json':continue
   name=prefix+f.relative_to(root).as_posix();data=f.read_bytes();digest=hashlib.sha256(data).hexdigest();blob='blobs/'+digest
   assert name not in manifest;manifest[name]=dict(blob=blob,bytes=len(data),sha256=digest)
   if blob not in blobs:z.writestr(blob,data);blobs.add(blob)
 z.writestr('files.json',json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 for row in manifest.values():
  data=z.read(row['blob']);assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
index={k:v for k,v in receipt.items() if k not in ['reports','sourceInputs','binaryInputs']}
index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),originalFiles=len(manifest),uniquePayloads=len(blobs),allOriginalPayloadsPreserved=True)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n');print(json.dumps(index))
