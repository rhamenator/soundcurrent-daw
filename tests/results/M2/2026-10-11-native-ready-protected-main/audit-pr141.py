from pathlib import Path
import subprocess,json,hashlib,datetime
folder=Path('.cache/native-ready-consumer/main-review');review=json.loads((folder/'reviewed-blobs.json').read_text());repo='repos/rhamenator/soundcurrent-daw'
def api(path):return json.loads(subprocess.check_output(['gh','api',path],text=True))
assert api(repo+'/commits/main')['sha']==review['base']
p=api(repo+'/pulls/141');assert p['state']=='open' and not p['draft'];assert p['head']['sha']==review['head'];assert p['base']['sha']==review['base'];assert p['mergeable'] is True
assert subprocess.check_output(['git','status','--porcelain'],text=True)==''
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==review['head']
for n,r in review['files'].items():
 assert hashlib.sha256(Path(n).read_bytes()).hexdigest()==r['sha256']
 assert subprocess.check_output(['git','rev-parse',review['head']+':'+n],text=True).strip()==r['gitBlob']
protection=api(repo+'/branches/main/protection');assert protection['enforce_admins']['enabled'] is True;assert protection['required_status_checks']['strict'] is True;required=protection['required_status_checks']['checks'];assert len(required)==4 and all(c['app_id']==15368 for c in required)
checks=api(repo+'/commits/'+review['head']+'/check-runs?per_page=100');assert checks['total_count']<=100
selected=[]
for c in required:
 found=[r for r in checks['check_runs'] if r['name']==c['context'] and r['app']['id']==c['app_id']]
 assert found,(c['context'],0);found.sort(key=lambda r:r['id'],reverse=True);assert found[0]['status']=='completed' and found[0]['conclusion']=='success',(c['context'],found[0]['status'],found[0]['conclusion'])
 assert found[0]['head_sha']==review['head'];selected.append(found[0])
query='query {repository(owner:"rhamenator",name:"soundcurrent-daw"){pullRequest(number:141){reviewThreads(first:100){pageInfo{hasNextPage}nodes{id isResolved}}}}}'
graph=json.loads(subprocess.check_output(['gh','api','graphql','-f','query='+query],text=True));threads=graph['data']['repository']['pullRequest']['reviewThreads'];assert not threads['pageInfo']['hasNextPage'];assert all(r['isResolved'] for r in threads['nodes'])
assert subprocess.check_output(['git','rev-parse',review['mergeRef']+'^{tree}'],text=True).strip()==review['tree']
assert p['merge_commit_sha']==review['mergeRef']
assert p['mergeable_state']=='clean',p['mergeable_state']
binding=json.loads((folder/'qualified-ci-log-binding.json').read_text());assert binding['head']==review['head'] and binding['base']==review['base'] and binding['testedMerge']==review['mergeRef'] and binding['tree']==review['tree'];assert set(binding['jobs'])=={str(r['id']) for r in selected}
out={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'pullRequest':p['html_url'],'head':review['head'],'base':review['base'],'tree':review['tree'],'reviewedBlobs':review['files'],'requiredChecks':[{'name':r['name'],'id':r['id'],'appId':r['app']['id'],'conclusion':r['conclusion']}for r in selected],'unresolvedThreads':0,'policyChanged':False,'adminBypass':False,'productArtifactUploaded':False}
(folder/'pr141-admission-audit.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'head':out['head'],'requiredGreen':len(out['requiredChecks']),'reviewedBlobs':len(out['reviewedBlobs']),'ordinaryMergeAdmitted':True}))
