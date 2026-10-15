#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain actual repeated pair production, adoption and unchanged legacy evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for n in ['build','evidence','public']:p.add_argument(n,type=Path)
p.add_argument('--checkpoint',type=int,choices=[294,295],default=294)
a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if (n.startswith(('include/','src/','ui/','third_party/','tests/','tools/','.github/')) and not n.startswith('tests/results/')) or n in ['CMakeLists.txt','research/linux-ci-test-durations.json']]
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-native-media-path-tests','sc-session-tests','sc-comp-repeat-fixture','sc-comp-handle-plan-tests','sc-comp-regeneration-source-tests','sc-comp-assembly-ui-tests','sc-comp-processing-plan-tests','sc-comp-regeneration-history-tests','sc-comp-span-v2-fixture','sc-comp-assembly-job-tests','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker']]+sorted(build.glob('lib*.a'))
binaries={str(n):sha(n) for n in inputs};evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
roots=[build/n for n in ['comp-regeneration-history-evidence','comp-span-v2-evidence','comp-assembly-ui-evidence','comp-regeneration-source-evidence','comp-repeat-evidence']]
if a.checkpoint==295:roots.append(build/'comp-paired-history-evidence')
for r in roots:r.mkdir(exist_ok=True)
before=[set(r.iterdir()) for r in roots]
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(native-media-long-path|session-state|comp-repeated-pair-production|comp-handle-envelope-plans|ci-shard-coverage|comp-transition-processing-permissions|comp-regeneration-source|comp-regeneration-history|comp-clocked-span-worker|desktop-comp-assembly-apply)$']
if a.checkpoint==295:command[-1]=command[-1].replace('native-media-long-path|','comp-paired-history-review|native-media-long-path|')
selected=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1','-R',command[-1]],text=True))
assert {t['name'] for t in selected['tests']}==set(command[-1][2:-2].split('|')), 'Missing qualification test'
(scope/'started.json').write_text(json.dumps(dict(sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command),indent=2)+'\n')
start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
seconds=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
runs=[];reports=[]
for root,old in zip(roots,before):
    new=set(root.iterdir())-old;assert len(new)==1;run=new.pop();runs.append(run);reports.append(json.loads((run/'report.json').read_text()))
history,v2,ui,plans,repeat=reports[:5]
assert repeat['actualPairedAdoptions']==9 and repeat['successfulWorkers']==15 and repeat['retainedAssembledMixedCases']==3
assert not repeat['applicationEntryEnabled'] and not repeat['qualityAdmission'] and not repeat['physicalAudio']
assert all(p['exitCode']==0 for p in repeat['processes'])
assert all(not p['observationTimedOut'] and not p['retirementPending'] for p in repeat['processes'] if p['kind']=='worker')
assert len(repeat['reports'])==9 and sum(r['mixedRetained'] for r in repeat['reports'])==3
assert sum(r['result']['lateHistoryBeforeCompletionRefused'] for r in repeat['reports'])==1
assert all(r['result']['undoRedo'] and r['result']['savedReopened'] and r['result']['joinedMemoryReleased'] and r['result']['queuedStaleApplyRefused'] and r['result']['historyFailureBeforePublication'] and r['result']['changedHistoryVerificationRefused'] and r['result']['unchangedSamples']>0 and r['result']['liveExportSamples']>0 for r in repeat['reports'])
assert all(r['guardedPlans']==r['plannedRecipes']==4 and r['ancestorDependencyRefusals']>=8 and r['joinedMemoryReleased'] and r['stateUnchanged'] for r in plans['reports'])
assert all(p['exitCode']==0 for p in plans['resolverProcesses'])
reviews=[c for r in history['reports'] for c in r['reviews']]
assert len(reviews)==6 and all(c['queuedDependencyGuard'] and c['queuedStaleRefusals']==8 for c in reviews)
assert all(r['projectBytesUnchanged'] and r['joinedMemoryReleased'] and not r['newRendering'] and not r['qualityAdmission'] for r in history['reports'])
assert all(p['exitCode']==0 for p in history['reviewProcesses'])
assert v2['successfulV2Workers']==9 and v2['refusedV2Workers']==4 and not v2['applicationEntryEnabled'] and not v2['pairedAssemblyExercised'] and v2['projectBytesUnchanged']
assert all(r['verification']['historyReviewed'] and r['verification']['joinedMemoryReleased'] and not r['verification']['assemblyAdoption'] for r in v2['reports'])
workers=[r for r in v2['processes'] if r['kind']=='v2-worker'];assert len(workers)==13
assert sum(r['exitCode']==0 for r in workers)==9 and all(not r['retirementPending'] and not r['observationTimedOut'] for r in workers)
assert all(r['exitCode']==0 for r in v2['processes'] if r['kind']!='v2-worker')
cases=[c for r in ui['reports'] for c in r['cases']];normal=[c for c in cases if c['case']==0]
assert len(cases)==6 and len(normal)==3 and all(c['asynchronousClose'] for c in cases)
assert all(c['queuedDependencyGuard'] and c['undoRedoSaveReopen'] and c['rawAndForeignLedgerRefused'] and c['exportPeak']>1 for c in normal)
assert all(r['mainWindowIntegrated'] and not r['physicalAudio'] and not r['qualityAdmission'] and r['executorSha256']==sha(build/'sc-comp-assembly-ui-tests') for r in ui['reports'])
expanded=next(r['cases'] for r in ui['reports'] if len(r['cases'])==4)
assert expanded[1]['changedFileRefused'] and expanded[2]['sameRootReopenRefused'] and expanded[3]['closeCanceledWithoutEdit'] and expanded[3]['guiTicks']>0
assert all(p['exitCode']==0 for p in ui['controllerProcesses'])
with zipfile.ZipFile(repo/'tests/results/M2/2026-10-14-comp-span-v2/acceptance.zip') as z:old=json.loads(z.read('qualification.json'))
legacy=[r['adoption']['exportFileSha256'] for r in old['reports'][0]['inputs']['reports']]
assert [c['exportFileSha256'] for c in normal]==legacy
assert [r['adoption']['exportFileSha256'] for r in history['inputs']['reports']]==legacy
assert [r['adoption']['exportFileSha256'] for r in v2['inputWorkflow']['reports']]==legacy
assert [r['adoption']['exportFileSha256'] for r in repeat['inputWorkflow']['reports']]==legacy
for workflow in [history['inputs']['inputWorkflow'],v2['inputWorkflow']['inputWorkflow'],ui['inputWorkflow'],plans['inputs']['inputWorkflow'],repeat['inputWorkflow']['inputWorkflow']]:
    processes=[p for p in workflow['processes'] if p['kind'] in ['worker','nominal-worker']]
    assert len(processes)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in processes)
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=a.checkpoint,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=seconds,sourceAndBinariesUnchanged=True,reports=reports,queuedHistoricalGuards=6,queuedStaleRefusals=48,layeredGuards=12,ancestorDependencyRefusals=sum(r['ancestorDependencyRefusals'] for r in plans['reports']),historyChecks=sum(r['checks'] for r in history['reports']),historyRefusals=sum(len(r['refusals']) for r in history['reports']),historicalPcmComparisons=sum(c['comparedSamples'] for c in reviews),successfulV2Workers=9,refusedV2Workers=4,workerPcmComparisons=v2['completePcmSamples'],mainWindowCases=6,legacyExportHashesUnchanged=True,allObservedProcessesReaped=True,applicationEntryEnabled=False,pairedAssemblyExercised=True,actualPairedAdoptions=9,successfulRepeatedWorkers=15,retainedAssembledMixedCases=3,lateHistoryBeforeCompletionRefusals=1,unchangedPlaybackSamples=sum(r['result']['unchangedSamples'] for r in repeat['reports']),liveExportSamples=sum(r['result']['liveExportSamples'] for r in repeat['reports']),nativeWindowsQualified=False,physicalAudio=False,qualityAdmission=False,fullDawParity=False)
if a.checkpoint==295:
    pairedReport=reports[5]
    assert all(p['exitCode']==0 for p in pairedReport['reviewProcesses'])
    assert all(r['projectBytesUnchanged'] and r['joinedMemoryReleased'] and r['newRendering'] and not r['reviewerRendersNewAudio'] and not r['qualityAdmission'] for r in pairedReport['reports'])
    paired=[r['pairedReview'] for r in pairedReport['reports']]
    assert len(paired)==3
    for r in paired:
        assert r['separateAssemblies']==2 and r['pairedAssemblies']==1 and r['disjointAssembliesVerified']==2
        assert r['separateSamples']==2*r['pairedSamples'] and r['pairedSamples']>0
        assert r['additionalOriginalAssemblyFixtures']==1
        assert all(r[k] for k in ['bothOrders','staleCoveredSourceRefused','crossOperationCorruptionRefused','coveredOutputCorruptionRefused','cancellationRefused','disjointCorruptionRefused'])
    with zipfile.ZipFile(repo/'tests/results/M2/2026-10-14-comp-repeated-pairs/acceptance.zip') as z:
        previous=json.loads(z.read('qualification.json'))
    assert [r['result']['exportSha256'] for r in repeat['reports']]==[r['result']['exportSha256'] for r in previous['reports'][4]['reports']]
    receipt.update(pairedHistoryReviews=paired,repeatedExportHashesUnchangedFrom294=True,additionalOriginalAssemblyFixtures=3)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
manifest={};blobs=set()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    # Keep the receipt directly readable, and de-duplicate identical original
    # payloads across independently generated corpora without discarding bytes.
    z.write(scope/'qualification.json','qualification.json')
    evidenceRoots=[(scope,''),(runs[0],'history/'),(runs[1],'v2-workers/'),(runs[2],'main-window/'),(runs[3],'layered-planning/'),(runs[4],'repeated-pairs/')]
    if a.checkpoint==295:evidenceRoots.append((runs[5],'paired-history/'))
    for root,prefix in evidenceRoots:
        for f in sorted(root.rglob('*')):
            if not f.is_file() or f==scope/'qualification.json':continue
            name=prefix+f.relative_to(root).as_posix();b=f.read_bytes();digest=hashlib.sha256(b).hexdigest();blob='blobs/'+digest
            assert name not in manifest;manifest[name]=dict(blob=blob,bytes=len(b),sha256=digest)
            if blob not in blobs:z.writestr(blob,b);blobs.add(blob)
    z.writestr('files.json',json.dumps(manifest,indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None and json.loads(z.read('files.json'))==manifest
    for name,row in manifest.items():
        b=z.read(row['blob']);assert len(b)==row['bytes'] and hashlib.sha256(b).hexdigest()==row['sha256'],name
    entries=len(z.namelist())
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','reports']}
index['archive']=dict(format='sc-hash-file-evidence-v1',fileManifest='files.json',receipt='qualification.json',bytes=archive.stat().st_size,sha256=sha(archive),entries=entries,originalFiles=len(manifest),uniquePayloads=len(blobs),allOriginalPayloadsPreserved=True)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),runs=[str(r) for r in runs],archive=index['archive'],seconds=seconds,historyChecks=receipt['historyChecks'],historyRefusals=receipt['historyRefusals'],historicalPcmComparisons=receipt['historicalPcmComparisons'],workerPcmComparisons=receipt['workerPcmComparisons'])))
