#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain every terminal outcome and complete PCM file, including failed cases."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, resource, subprocess, time
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
p = argparse.ArgumentParser()
p.add_argument('probe', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--build-receipt', type=Path, required=True)
args = p.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parent
probe = args.probe.resolve()
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
receipt = json.loads(args.build_receipt.read_text())
assert receipt['exitCode'] == 0 and receipt['unchangedInputs'] and receipt['probeSha256'] == sha(probe)
contract = json.loads((source/'contract.json').read_text())
for name in ['contract.json', 'analyze_bank.py', 'run_bank.py', 'render_probe.cpp', 'protected_plan.cpp', 'protected_plan.hpp']:
    path = source/name
    assert receipt['preflight']['files'][str(path)] == sha(path)
    (root/name).write_bytes(path.read_bytes())
(root/'build-receipt.json').write_bytes(args.build_receipt.read_bytes())
started = time.monotonic()
records = []
def limits():
    ceiling = contract['limits']['linuxAddressSpaceMiB']*1024*1024
    resource.setrlimit(resource.RLIMIT_AS, (ceiling, ceiling))
for family in contract['bank']['families']:
    for profile in contract['bank']['profiles']:
        deadline = min(contract['limits']['childSeconds'], contract['limits']['bankSeconds']-(time.monotonic()-started))
        request = {'family': family, 'profile': profile}
        row = {'id': len(records), 'request': request, 'waves': {}}
        if deadline <= 0:
            row.update(exitCode=None, timeout=False, notStarted='bank deadline exhausted')
        else:
            directory = root/f"case-{row['id']:02d}"
            childStart = time.monotonic()
            child = subprocess.Popen([str(probe), json.dumps(request, separators=(',', ':')), str(directory)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, preexec_fn=limits)
            timeout = False
            try:
                out, err = child.communicate(timeout=deadline)
            except subprocess.TimeoutExpired:
                timeout = True
                child.kill()
                out, err = child.communicate()
            row.update(pid=child.pid, exitCode=child.returncode, timeout=timeout, seconds=time.monotonic()-childStart, stdout=out, stderr=err)
            if directory.exists():
                row['waves'] = {path.name: {'sha256': sha(path), 'bytes': path.stat().st_size} for path in sorted(directory.glob('*.wav'))}
            if child.returncode == 0:
                row['result'] = json.loads(out)
        records.append(row)
        (root/'processes.json').write_text(json.dumps(records, indent=2)+'\n')
summary = {'format': 'sc-protected-warp-bank-v1', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
           'actualProcesses': sum('pid' in r for r in records), 'terminalSuccesses': sum(r['exitCode'] == 0 for r in records),
           'terminalFailures': sum(r['exitCode'] not in [0, None] for r in records), 'notStarted': sum('notStarted' in r for r in records),
           'seconds': time.monotonic()-started, 'limits': contract['limits'], 'probeSha256': sha(probe),
           'buildReceiptSha256': sha(args.build_receipt), 'sourceHashesBeforeRun': {name: sha(root/name) for name in ['contract.json', 'analyze_bank.py', 'run_bank.py', 'render_probe.cpp', 'protected_plan.cpp', 'protected_plan.hpp']},
           'sourceBase': contract['sourceBase'], 'sourceCommit': receipt['preflight']['sourceCommit'], 'sourceWasUncommitted': True,
           'nativeAudio': False, 'shippingAdopted': False, 'fullQualityQualified': False}
(root/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary))
