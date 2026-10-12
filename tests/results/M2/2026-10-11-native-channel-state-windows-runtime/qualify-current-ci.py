from pathlib import Path
import subprocess,json,hashlib,re
folder=Path('.cache/native-channel-state/runtime-ci-review');repo='repos/rhamenator/soundcurrent-daw';runid=38174473151
api=lambda path:json.loads(subprocess.check_output(['gh','api',path],text=True))
review=json.loads((folder/'reviewed-blobs.json').read_text());run=api(repo+'/actions/runs/'+str(runid))
assert run['status']=='completed' and run['conclusion']=='success' and run['head_sha']==review['head'],{k:run[k]for k in ['status','conclusion','head_sha']}
p=api(repo+'/pulls/144');assert p['state']=='open' and p['head']['sha']==review['head'] and p['base']['sha']==review['base'];merge=p['merge_commit_sha']
subprocess.check_call(['git','fetch','origin','refs/pull/144/merge'])
assert subprocess.check_output(['git','rev-parse','FETCH_HEAD'],text=True).strip()==merge
assert subprocess.check_output(['git','rev-parse',merge+'^{tree}'],text=True).strip()==review['tree']
assert set(subprocess.check_output(['git','show','-s','--format=%P',merge],text=True).split())=={review['head'],review['base']}
paths=subprocess.check_output(['git','diff','--name-only',review['base'],review['head']],text=True).splitlines();assert set(paths)==set(review['files'])
for n,r in review['files'].items():assert subprocess.check_output(['git','rev-parse',merge+':'+n],text=True).strip()==r['gitBlob']
response=api(repo+'/actions/runs/'+str(runid)+'/jobs?per_page=100');jobs=response['jobs'];assert response['total_count']==len(jobs)==4
required=api(repo+'/branches/main/protection')['required_status_checks']['checks'];assert {c['context']for c in required}=={j['name']for j in jobs} and all(c['app_id']==15368 for c in required)
logs=folder/'qualified-joblogs';logs.mkdir(exist_ok=True);records={}
for job in jobs:
 assert job['status']=='completed' and job['conclusion']=='success'
 raw=subprocess.check_output(['gh','api',repo+'/actions/jobs/'+str(job['id'])+'/logs'])
 assert 1024<len(raw)<4*1024*1024
 text=raw.decode('utf-8-sig');assert merge in text and ('Merge '+review['head']+' into '+review['base'])in text and 'checkout' in text
 (logs/(str(job['id'])+'.txt')).write_bytes(raw)
 records[str(job['id'])]={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'checkout':merge,'conclusion':'success'}
review['mergeRef']=merge;(folder/'reviewed-blobs.json').write_text(json.dumps(review,indent=2)+'\n')
binding={'head':review['head'],'base':review['base'],'testedMerge':merge,'tree':review['tree'],'jobs':records};(folder/'qualified-ci-log-binding.json').write_text(json.dumps(binding,indent=2)+'\n')
print({'head':review['head'],'testedMerge':merge,'jobs':len(records),'exactBlobs':len(paths)})
