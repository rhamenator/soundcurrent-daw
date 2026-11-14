from pathlib import Path
import hashlib,json,zipfile,importlib.util,tempfile
r=Path('.cache/comp-transition-ui/pr194-timeline-creation-review');public=Path('tests/results/M2/2026-10-14-comp-regeneration-plans/original-ci');public.mkdir(exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
run=json.loads((r/'run-api.json').read_text());jobs=json.loads((r/'jobs-api.json').read_text());artifacts=json.loads((r/'artifacts-api.json').read_text())
assert run['head_sha']=='426313713300099076c9f494c58756d65a5875e4' and run['status']=='completed' and run['conclusion']=='failure'
assert len(jobs['jobs'])==4 and all(j['status']=='completed' for j in jobs['jobs'])
result=dict(run=run['id'],head=run['head_sha'],conclusion=run['conclusion'],jobs=[],artifacts=[],workflows={})
for j in jobs['jobs']:
 p=r/f"job-{j['id']}.log";assert p.stat().st_size>1000
 result['jobs'].append(dict(id=j['id'],name=j['name'],conclusion=j['conclusion'],logBytes=p.stat().st_size,logSha256=sha(p.read_bytes())))
for a in artifacts['artifacts']:
 path=r/f"artifact-{a['id']}.zip";assert path.stat().st_size==a['size_in_bytes'] and 'sha256:'+sha(path.read_bytes())==a['digest']
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None
  names=z.namelist();result['artifacts'].append(dict(id=a['id'],name=a['name'],bytes=path.stat().st_size,sha256=a['digest'],entries=len(names),pngs=sum(n.endswith('.png') for n in names)))
  if a['name'] not in ['linux-synthetic-test-log','windows-desktop-import-test-log']:continue
  platform='linux' if a['name'].startswith('linux') else 'windows'
  for family,caseCount,checks in [('comp-assembly-ui',6,87),('comp-assembly-controller',15,171),('comp-transition-ui',8,174)]:
   reports=[n for n in names if family+'-evidence/run-' in n and n.endswith('/report.json') and '/case-' not in n and '/inputs/' not in n]
   if platform=='windows' and family=='comp-transition-ui':
    assert not reports
    partial=[n for n in names if family+'-evidence/run-' in n and n.endswith('case-44100/report.json')];assert len(partial)==1
    d=json.loads(z.read(partial[0]));assert len(d['cases'])==1 and d['cases'][0]['undoRedoSaveReopen'] and d['cases'][0]['noAudioBeforeApply']
    result['workflows'][platform+'/'+family]=dict(complete=False,completedRateReports=[44100],failure='Unicode path converted through Windows legacy code page in missing-helper case; no complete 48/96 kHz report')
    (public/(platform+'-'+family+'-partial.json')).write_bytes(z.read(partial[0]));continue
   assert len(reports)==1
   d=json.loads(z.read(reports[0]));rows=[c for p in d['reports'] for c in p['cases']]
   assert len(rows)==caseCount and sum(p['checks'] for p in d['reports'])==checks
   assert all(p['exitCode']==0 for p in d['controllerProcesses'])
   if family.endswith('controller'):assert all(c['joinedMemoryReleased'] and c['closed']['closed'] for c in rows)
   else:assert all(c['asynchronousClose'] for c in rows)
   if family.endswith('transition-ui'):
    assert all(c['noAudioBeforeApply'] and c['undoRedoSaveReopen'] and all(p['reaped'] and p['exitCode']==0 for p in c['newChildren']) for c in rows if c['case']==0)
   result['workflows'][platform+'/'+family]=dict(complete=True,cases=len(rows),checks=checks)
   (public/(platform+'-'+family+'.json')).write_bytes(z.read(reports[0]))
  if platform=='windows':
   logs=[n for n in names if n.endswith('/Testing/Temporary/LastTest.log')];assert len(logs)==1;b=z.read(logs[0])
   try:b.decode('utf-8');raise AssertionError('Expected retained original non-UTF8 diagnostic')
   except UnicodeDecodeError:pass
   spec=importlib.util.spec_from_file_location('retainer','.github/scripts/retain-controller-failure-projects.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   assert not any(line.startswith(marker.encode('ascii')) for line in b.splitlines() for marker in module.POLICIES)
   with tempfile.TemporaryDirectory(dir=r) as temp:
    log=Path(temp)/'Testing/Temporary/LastTest.log';log.parent.mkdir(parents=True);log.write_bytes(b);assert module.retain(temp)==[] and log.read_bytes()==b
   result['originalWindowsMixedEncodingLogAcceptedWithoutModification']=True
   result['originalWindowsCTestLogSha256']=sha(b)
assert len(result['artifacts'])==4
result.update(allOriginalArchivesAndPngsRetained=True,fullOriginalLogsAndApisRetained=True,windowsDeploymentBundleProduced=False,windowsDeploymentBundleAudit='Unavailable: desktop test failure skipped collection; no new installation claim',linuxFailure='desktop-paired-crossfade: 350px dialog, viewport312/body323/horizontal11; wrapping option captions corrected locally',windowsFailure='desktop-comp-transition-create: mode4 filesystem path .string conversion; corrected to explicit UTF8',windowsRetentionFailure='Mixed-encoding unrelated diagnostic caused strict whole-log decode; marker-only strict UTF8 verified on retained original log',qualityAdmission=False,newNativeWindowsQualification=False)
(public/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
for name in ['run-api.json','jobs-api.json','artifacts-api.json']: (public/name).write_bytes((r/name).read_bytes())
(public/'audit-original-ci.py').write_bytes(Path(__file__).read_bytes())
print(json.dumps({k:v for k,v in result.items() if k not in ['jobs','artifacts']}))
