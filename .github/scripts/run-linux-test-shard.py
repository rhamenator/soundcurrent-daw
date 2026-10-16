#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Partition the entire discovered CTest inventory; never shorten test deadlines."""
from pathlib import Path
import argparse,json,re,subprocess,sys

def partition(names, estimates, count):
    if count<1 or len(names)<count or len(set(names))!=len(names):
        raise ValueError('Invalid CTest inventory or shard count')
    rows=[dict(names=[],estimatedSeconds=0.0) for _ in range(count)]
    def weight(name):
        value=estimates.get(name,60.0)
        if type(value) not in (float,int) or not 0<=value<86400:
            raise ValueError('Invalid scheduling estimate')
        return max(float(value),0.01)
    for name in sorted(names,key=lambda n:(-weight(n),n)):
        target=min(range(count),key=lambda i:(rows[i]['estimatedSeconds'],i))
        rows[target]['names'].append(name);rows[target]['estimatedSeconds']+=weight(name)
    flat=[name for row in rows for name in row['names']]
    if sorted(flat)!=sorted(names) or len(set(flat))!=len(flat) or any(not r['names'] for r in rows):
        raise ValueError('Incomplete or overlapping CTest partition')
    return rows

def main():
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('shard',type=int);p.add_argument('--count',type=int,default=2);a=p.parse_args()
    if not 0<=a.shard<a.count:raise ValueError('Invalid selected shard')
    repo=Path(__file__).resolve().parents[2];build=a.build.resolve()
    inventory=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))
    (build/'ci-test-inventory.json').write_text(json.dumps(inventory,indent=2)+'\n')
    names=[t['name'] for t in inventory['tests']]
    weights=json.loads((repo/'research/linux-ci-test-durations.json').read_text())['seconds']
    rows=partition(names,weights,a.count);selected=rows[a.shard]['names']
    pattern='^('+ '|'.join(re.escape(n) for n in selected)+')$'
    command=['ctest','--test-dir',str(build),'--output-on-failure','--no-tests=error','--parallel','1','-R',pattern]
    # Check CTest's own interpretation as well as the Python partition: exact
    # coverage must not depend on regex dialect assumptions or matching prefixes.
    matched=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1','-R',pattern],text=True))
    if sorted(t['name'] for t in matched['tests'])!=sorted(selected):raise ValueError('CTest selection differs from complete partition')
    plan=dict(shard=a.shard,inventoryCount=len(names),shards=rows,selectedCount=len(selected),command=command,allTestsAssignedExactlyOnce=True,deadlinesUnchanged=True)
    output=build/'ci-shard-plan.json';output.write_text(json.dumps(plan,indent=2)+'\n')
    result=subprocess.run(command);plan['exitCode']=result.returncode;output.write_text(json.dumps(plan,indent=2)+'\n');return result.returncode

if __name__=='__main__':sys.exit(main())
