#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Source/helper binding and receipt refusal fixtures, without installer/audio execution."""
from pathlib import Path
from copy import deepcopy
import json,importlib.util
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import protected_warp_qualification as gate
import package_linux_preview as linux_builder
import package_windows_preview as windows_builder
root=ROOT/'tests/fixtures/protected-warp-qualification'
checks=0
for path,platform in [('linux.json','linux'),('windows.json','win32')]:
 original=json.loads((root/path).read_text());expected=(original['sourceCommit'],original['sourceTree'],original['workerSha256'],platform)
 assert gate.qualify_protected_warp(original,*expected)['fullPcmComparisons']==18;checks+=1
 mutations={
  'source':lambda q:q.update(sourceCommit='0'*40),
  'tree':lambda q:q.update(sourceTree='0'*40),
  'helper':lambda q:q.update(workerSha256='0'*64),
  'platform':lambda q:q.update(platform='Linux-fake' if platform=='win32' else 'Windows-fake'),
  'native_scope':lambda q:q.update(nativeAudio=True),
  'quality_scope':lambda q:q.update(fullQualityQualified=True),
  'rights':lambda q:q.update(syntheticSourceRights='unlicensed'),
  'count_boolean':lambda q:q.update(workerProcesses=True),
  'count_float':lambda q:q.update(completedRenders=21.0),
  'check_shortfall':lambda q:q.update(checks=516),
  'lost_worker':lambda q:q['workers'].pop(),
  'crashed_worker':lambda q:q['workers'][0].update(exit=-9),
  'boolean_exit':lambda q:q['workers'][0].update(exit=False),
  'zero_pid':lambda q:q['workers'][0].update(pid=0),
  'duplicate_operation':lambda q:q['workers'][1]['request'].update(operation=q['workers'][0]['request']['operation']),
  'source_identity':lambda q:q['workers'][0]['completion'][0].update(sourceSha256='0'*64),
  'origin':lambda q:q['workers'][0]['completion'][0].update(first=17),
  'origin_boolean':lambda q:q['workers'][0]['completion'][0].update(first=False),
  'raw_extent':lambda q:q['workers'][0]['completion'][0].update(sourceFrames=1),
  'target':lambda q:q['workers'][0]['completion'][0].update(target=1),
  'incomplete':lambda q:q['workers'][0]['completion'][0].update(complete=False),
  'missing_completion':lambda q:q['workers'][0].update(completion=[]),
  'complete_after_refusal':lambda q:q['workers'][-1].update(completion=[q['workers'][0]['completion'][0]]),
  'metric':lambda q:q['workers'][0]['completion'][0].update(memoryMetric='RSS'),
  'ceiling':lambda q:q['workers'][0]['completion'][0].update(payloadPeakBytes=268435456),
  'gap_policy':lambda q:q['workers'][0]['request']['warp'].update(after=2049),
  'typed_warp':lambda q:q['workers'][0]['completion'][0]['warp']['points'][0]['source'].__setitem__(0,0.0),
  'warp_boundary':lambda q:q['workers'][0]['completion'][0]['warp']['spans'][0]['sourceBegin'].__setitem__(0,3841),
  'ready_key':lambda q:q['workers'][0]['ready'].update(renderKey='0'*64),
  'comparison_equal_flag':lambda q:q['comparisons'][0].update(allPcmEqual=False),
  'comparison_hash':lambda q:q['comparisons'][0].update(workerPcmSha256='0'*64),
  'comparison_frames':lambda q:q['comparisons'][0].update(frames=1),
  'missing_comparison':lambda q:q['comparisons'].pop(),
  'duplicate_comparison':lambda q:q['comparisons'].__setitem__(1,deepcopy(q['comparisons'][0])),
  'bad_family_type':lambda q:q['comparisons'][0].update(family=[]),
  'bad_operation_type':lambda q:q['comparisons'][0].update(operation=[]),
  'mutation_scope':lambda q:q['sourceMutation'].update(changedRawTiming='other'),
 }
 if platform=='win32':mutations['mutation_denied_claim']=lambda q:q['sourceMutation']['heldReadWriteDenied'].update(rawUnchanged=False)
 for name,mutate in mutations.items():
  q=deepcopy(original);mutate(q)
  try:gate.qualify_protected_warp(q,*expected)
  except ValueError:checks+=1
  else:raise AssertionError('Inconsistent '+platform+' evidence accepted: '+name)
 assert original==json.loads((root/path).read_text())
import hashlib,tempfile
with tempfile.TemporaryDirectory(prefix='sc-protected-package-inputs-') as temporary:
 binary=Path(temporary)/'sc-stretch-render-worker';binary.write_bytes(b'owned-binding-fixture')
 q=json.loads((root/'linux.json').read_text());q['workerSha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
 assert linux_builder.qualify_protected_worker(q,q['sourceCommit'],q['sourceTree'],binary)['fullPcmComparisons']==18;checks+=1
 binary.write_bytes(b'stale-binding-fixture')
 try:linux_builder.qualify_protected_worker(q,q['sourceCommit'],q['sourceTree'],binary)
 except ValueError:checks+=1
 else:raise AssertionError('Stale Linux packaged helper accepted')
 q=json.loads((root/'windows.json').read_text());manifest={'files':[{'path':'sc-stretch-render-worker.exe','sha256':q['workerSha256']}]}
 assert windows_builder.qualify_protected_worker(manifest,q,q['sourceCommit'],q['sourceTree'])['fullPcmComparisons']==18;checks+=1
 for malformed in [None,{**q,'workerSha256':'0'*64}]:
  try:windows_builder.qualify_protected_worker(manifest,malformed,q['sourceCommit'],q['sourceTree'])
  except ValueError:checks+=1
  else:raise AssertionError('Missing/stale Windows protected receipt accepted')
 for files in [[],manifest['files']*2]:
  try:windows_builder.qualify_protected_worker({'files':files},q,q['sourceCommit'],q['sourceTree'])
  except ValueError:checks+=1
  else:raise AssertionError('Missing/duplicate Windows protected helper accepted')
print('protected packaging gate checks=',checks,'native_audio_replayed=false product_binary_executed=false')
