#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain source-bound regeneration plans, legacy recipes and transition UI checks."""
from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,time,zipfile
repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for n in ['build','evidence','public']:p.add_argument(n,type=Path)
a=p.parse_args();build=a.build.resolve();evidence=a.evidence.resolve();public=a.public.resolve()
def git(*args):return subprocess.check_output(['git',*args],cwd=repo,text=True).strip()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert not git('status','--porcelain');head=git('rev-parse','HEAD');tree=git('rev-parse','HEAD^{tree}')
files=[n for n in git('ls-files').splitlines() if (n.startswith(('include/','src/','ui/','third_party/','tests/','tools/','.github/')) and not n.startswith('tests/results/')) or n=='CMakeLists.txt']
sources={n:sha(repo/n) for n in files}
inputs=[build/n for n in ['sc-comp-regeneration-source-tests','sc-comp-handle-plan-tests','sc-comp-processing-recipe-tests','sc-comp-processing-plan-tests','sc-comp-transition-ui-tests','sc-crossfade-ui-tests','sc-comp-assembly-job-tests','sc-comp-span-worker','sc-comp-span-worker-fixture','sc-comp-pair-recovery-fixture','sc-comp-pair-assembly-tests','sc-stretch-render-worker']]+sorted(build.glob('lib*.a'))
binaries={str(n):sha(n) for n in inputs};evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='qualified-',dir=evidence))
runs=build/'comp-regeneration-source-evidence';runs.mkdir(exist_ok=True);before=set(runs.iterdir());guiRuns=build/'comp-transition-ui-evidence';guiRuns.mkdir(exist_ok=True);beforeGui=set(guiRuns.iterdir())
command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','-R','^(comp-regeneration-source|comp-handle-envelope-plans|comp-processing-recipe-recovery|comp-transition-processing-permissions|desktop-comp-transition-create|desktop-paired-crossfade)$'];start=time.monotonic()
with (scope/'tests.stdout').open('w') as out,(scope/'tests.stderr').open('w') as err:
    proc=subprocess.Popen(command,cwd=repo,stdout=out,stderr=err);code=proc.wait()
seconds=time.monotonic()-start;(scope/'LastTest.log').write_bytes((build/'Testing/Temporary/LastTest.log').read_bytes());assert code==0,(scope,code)
new=set(runs.iterdir())-before;assert len(new)==1;run=new.pop();report=json.loads((run/'report.json').read_text());resolved=report['reports'];assert len(resolved)==3
assert all(r['plannedRecipes']==4 and r['legacyRecipes']==2 and r['mixedAdmissionRefused'] for r in resolved)
assert all(r['joinedMemoryReleased'] and r['stateUnchanged'] and not r['fileVerification'] and not r['newRendering'] and not r['qualityAdmission'] for r in resolved)
assert sum(len(r['actualAdoptedSourceCases']) for r in resolved)==6 and sum(r['metadataOnlyAdditionalGenerations'] for r in resolved)==9
assert sorted({c['sourceRate'] for r in resolved for c in r['actualAdoptedSourceCases']})==[44100,48000,96000]
assert all(c['fractionalPositionsCompared']==c['currentFrames'] and c['closureRecords']==2 for r in resolved for c in r['actualAdoptedSourceCases'])
assert len(report['resolverProcesses'])==3 and all(p['exitCode']==0 for p in report['resolverProcesses'])
parent=report['inputs'];assert len(parent['publicationProcesses'])==3 and all(p['exitCode']==0 for p in parent['publicationProcesses'])
assert all(r['adoption']['schema']=='1.23' and r['adoption']['undoRedo'] and r['adoption']['savedReopened'] and r['adoption']['liveEqualsExport'] for r in parent['reports'])
workers=[p for p in parent['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker']]
assert len(workers)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in workers)
newGui=set(guiRuns.iterdir())-beforeGui;assert len(newGui)==1;gui=newGui.pop();guiReport=json.loads((gui/'report.json').read_text())
assert len(guiReport['controllerProcesses'])==3 and all(p['exitCode']==0 for p in guiReport['controllerProcesses'])
rows=[c for r in guiReport['reports'] for c in r['cases']];assert len(rows)==8 and sum(r['checks'] for r in guiReport['reports'])==180
assert all(c['asynchronousClose'] for c in rows)
assert all(c['noAudioBeforeApply'] and c['undoRedoSaveReopen'] and len(c['newChildren'])==2 and all(p['exitCode']==0 and p['reaped'] for p in c['newChildren']) for c in rows if c['case']==0)
assert all(c['noChildLaunch'] for c in rows if c['case']!=0)
guiWorkers=[p for p in guiReport['inputWorkflow']['processes'] if p['kind'] in ['worker','nominal-worker']]
assert len(guiWorkers)==12 and all(p['exitCode']==0 and not p['retirementPending'] and not p['observationTimedOut'] for p in guiWorkers)
assert [c['exportFileSha256'] for c in rows if c['case']==0]==[r['adoption']['exportFileSha256'] for r in parent['reports']]
assert sources=={n:sha(repo/n) for n in files} and binaries=={str(n):sha(n) for n in inputs} and head==git('rev-parse','HEAD')
receipt=dict(checkpoint=289,sourceHead=head,sourceTree=tree,sourceInputs=sources,binaryInputs=binaries,command=command,pid=proc.pid,exitCode=code,seconds=seconds,sourceAndBinariesUnchanged=True,report=report,guiReport=guiReport,plannedRecipes=12,retainedLegacyRecipes=6,transitionUiCases=8,transitionUiChecks=180,actualAdoptedSources=6,metadataOnlyAdditionalGenerations=9,fractionalPositionsCompared=sum(c['fractionalPositionsCompared'] for r in resolved for c in r['actualAdoptedSourceCases']),checks=sum(r['checks'] for r in resolved),allProcessesReaped=True,newRendering=False,newFileVerification=False,nativeWindowsQualified=False,physicalAudio=False,qualityAdmission=False,fullDawParity=False)
(scope/'qualification.json').write_text(json.dumps(receipt,indent=2)+'\n');public.mkdir(parents=True,exist_ok=True);archive=public/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for root,prefix in [(scope,''),(run,'workflow/'),(gui,'transition-ui/')]:
        for f in sorted(root.rglob('*')):
            if f.is_file():z.write(f,prefix+f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None;entries={n:dict(bytes=len(z.read(n)),sha256=hashlib.sha256(z.read(n)).hexdigest()) for n in z.namelist()}
index={k:v for k,v in receipt.items() if k not in ['sourceInputs','binaryInputs','report','guiReport']};index['archive']=dict(bytes=archive.stat().st_size,sha256=sha(archive),entries=entries)
(public/'index.json').write_text(json.dumps(index,indent=2)+'\n')
print(json.dumps(dict(sourceHead=head,scope=str(scope),run=str(run),archiveBytes=archive.stat().st_size,entries=len(entries),seconds=seconds,checks=receipt['checks'],fractionalPositionsCompared=receipt['fractionalPositionsCompared'])))
