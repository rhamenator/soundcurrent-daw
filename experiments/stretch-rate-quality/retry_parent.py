#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retry only the original harness refusal, with unchanged DSP output."""
from pathlib import Path
import argparse
import json
import subprocess
from run import REPO, parent_workflow, sha

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('verifier', type=Path)
parser.add_argument('original_bank', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
bank, output, verifier = args.original_bank.resolve(), args.output.resolve(), args.verifier.resolve()
assert not subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=REPO)
rows = json.loads((bank/'results.json').read_bytes())
processes = json.loads((bank/'processes.json').read_bytes())
inputs = json.loads((bank/'inputs.json').read_bytes())
assert inputs['files'] == {p: sha((REPO/p).read_bytes()) for p in inputs['files']}
assert inputs['verifierSha256'] == sha(verifier.read_bytes())
failed = [r for r in rows if r.get('parentWorkflowPassed') is False]
assert len(failed) == 1 and failed[0]['complete'] is True
row = failed[0]
request = row['request']
ready = json.loads(processes[row['processIndex']]['stdout'].splitlines()[0])
assert ready['event'] == 'ready' and ready['operation'] == request['operation']
original = bank/f'rate-{row["rate"]}'
output.mkdir(parents=True, exist_ok=False)
p, signatures = parent_workflow(verifier, original, output/'project', request, ready, 15)
receipt = {'format': 'sc-stretch-quality-parent-retry-v1', 'originalSource': inputs['sourceCommit'], 'retrySource': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(), 'originalOperation': request['operation'], 'request': request, 'verifierSha256': sha(verifier.read_bytes()), 'copiedInputsOriginalsUnchanged': signatures, 'exitCode': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr, 'rerenderedAudio': False, 'originalBankChanged': False, 'nativeAudio': False, 'nativeWindowsQualified': False, 'QStretchQualified': False, 'QPitchQualified': False}
(output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print(json.dumps(receipt))
assert p.returncode == 0
