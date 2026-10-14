#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Resolve actual adopted source histories, keeping original producer evidence."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,tempfile
p=argparse.ArgumentParser()
for name in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture','resolver']:p.add_argument(name,type=Path)
p.add_argument('--evidence',type=Path,required=True);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True)
scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
command=[sys.executable,str(Path(__file__).with_name('comp_assembly_job_tests.py'))]+[str(getattr(a,n).resolve()) for n in ['worker','span_fixture','pair_fixture','assembly_fixture','nominal_worker','job_fixture']]+['--evidence',str(scope/'inputs')]
with (scope/'inputs.stdout').open('w') as out,(scope/'inputs.stderr').open('w') as err:
    proc=subprocess.Popen(command,stdout=out,stderr=err);code=proc.wait();assert code==0,(scope,code)
roots=list((scope/'inputs').glob('run-*'));assert len(roots)==1
legacy=json.loads((Path(__file__).resolve().parent/'fixtures/comp-regeneration-legacy-recipes.json').read_text(encoding='utf-8'))
assert legacy['format']=='sc-retained-legacy-recipes-v1' and legacy['sourceArchiveSha256']=='d348cb34c8ca0331f4c493b8811724ee761b84d5a752990b940381614a7e4a16'
requests={rate:[] for rate in [44100,48000,96000]};(scope/'legacy').mkdir()
assert len(legacy['requests'])==6
for rate in requests:
    for side in [0,1]:
        rows=[r for r in legacy['requests'] if r['rate']==rate and r['side']==side];assert len(rows)==1
        row=rows[0];b=row['requestBytes'].encode('utf-8');assert hashlib.sha256(b).hexdigest()==row['requestSha256']
        path=scope/'legacy'/f'{rate}-{side}.json';path.write_bytes(b);requests[rate].append(str(path))
reports=[];processes=[]
for root in sorted(roots[0].glob('inputs/run-*/case-*/project-*')):
    proc=subprocess.Popen([str(a.resolver.resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    rate=int(root.parent.name.split('-')[1])
    try:out,err=proc.communicate(json.dumps(dict(root=str(root),legacyRequests=requests[rate])),timeout=30)
    except BaseException:proc.kill();proc.communicate();raise
    (root/'resolver.stdout').write_text(out);(root/'resolver.stderr').write_text(err);processes.append(dict(pid=proc.pid,exitCode=proc.returncode));assert proc.returncode==0,(root,err);reports.append(json.loads(out))
assert len(reports)==3 and all(r['plannedRecipes']==4 and r['legacyRecipes']==2 and len(r['actualAdoptedSourceCases'])==2 and r['metadataOnlyAdditionalGenerations']==3 and r['stateUnchanged'] and r['joinedMemoryReleased'] and not r['newRendering'] and not r['fileVerification'] and not r['qualityAdmission'] for r in reports)
result=dict(inputs=json.loads((roots[0]/'report.json').read_text()),resolverProcesses=processes,reports=reports)
(scope/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(scope=str(scope),cases=6,metadataOnlyGenerations=9)))
