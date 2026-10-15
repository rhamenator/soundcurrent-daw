#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import importlib.util,json
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('shards',root/'.github/scripts/run-linux-test-shard.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
weights=json.loads((root/'research/linux-ci-test-durations.json').read_text())['seconds']
names=list(weights)+['new-test-with-no-estimate'];rows=module.partition(names,weights,3)
assert sorted(n for r in rows for n in r['names'])==sorted(names)
assert module.partition(list(reversed(names)),weights,3)==rows
assert max(r['estimatedSeconds'] for r in rows)-min(r['estimatedSeconds'] for r in rows)<max(weights.values())
for bad,count in [([],2),(['one'],2),(['same','same'],2),(['a','b'],0)]:
 try:module.partition(bad,{},count)
 except ValueError:pass
 else:raise AssertionError('Invalid inventory accepted')
print(json.dumps(dict(testsAssigned=len(names),shards=len(rows),coverageExactlyOnce=True,stableOrder=True,unknownTestsIncluded=True,estimatedSeconds=[r['estimatedSeconds'] for r in rows])))
