#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile an isolated probe with before/after identities of all supplied inputs."""
from pathlib import Path
import argparse, datetime, hashlib, json, shutil, subprocess, time

p = argparse.ArgumentParser()
p.add_argument('session_library', type=Path)
p.add_argument('rubberband_library', type=Path)
p.add_argument('output', type=Path)
args = p.parse_args()
repo = Path(__file__).resolve().parents[2]
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
compiler = Path(shutil.which('c++')).resolve()
libraries = [args.session_library.resolve(), args.rubberband_library.resolve()]
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
files = [compiler, *libraries, repo/'third_party/nlohmann/json.hpp']
for prefix in ['include', 'src', 'third_party/rubberband', 'experiments/warp-map', 'experiments/protected-warp']:
    files += sorted(f for f in (repo/prefix).rglob('*') if f.is_file() and f.suffix in ['.cpp', '.hpp', '.h', '.c', '.txt', '.json', '.py'])
files = list(dict.fromkeys(files))
before = {str(f): sha(f) for f in files}
exe = root/'sc-protected-warp-probe'
command = [str(compiler), '-std=c++20', '-O2', '-DNDEBUG', '-fno-fast-math', '-Wall', '-Wextra', '-Wpedantic',
           '-I'+str(repo/'include'), '-isystem', str(repo/'third_party'), '-isystem', str(repo/'third_party/rubberband'),
           str(repo/'experiments/protected-warp/render_probe.cpp'), str(repo/'experiments/protected-warp/protected_plan.cpp'),
           str(repo/'experiments/warp-map/warp_map.cpp'), '-Wl,--start-group', *map(str, libraries), '-Wl,--end-group', '-pthread', '-o', str(exe)]
pre = {'format': 'sc-protected-warp-build-preflight-v1', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
       'command': command, 'files': before, 'sourceCommit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
       'workingTreeStatus': subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True)}
(root/'preflight.json').write_text(json.dumps(pre, indent=2)+'\n')
started = time.monotonic()
child = subprocess.Popen(command, cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
timeout = False
try:
    out, err = child.communicate(timeout=60)
except subprocess.TimeoutExpired:
    timeout = True
    child.kill()
    out, err = child.communicate()
(root/'stdout.log').write_text(out)
(root/'stderr.log').write_text(err)
after = {str(f): sha(f) for f in files}
receipt = {'format': 'sc-protected-warp-build-receipt-v1', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
           'preflight': pre, 'preflightSha256': sha(root/'preflight.json'), 'pid': child.pid, 'exitCode': child.returncode,
           'timeout': timeout, 'seconds': time.monotonic()-started, 'after': after, 'unchangedInputs': before == after,
           'probeSha256': sha(exe) if exe.exists() else None}
(root/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
assert child.returncode == 0 and not timeout and before == after, receipt
print(json.dumps({k: receipt[k] for k in ['pid', 'exitCode', 'seconds', 'unchangedInputs', 'probeSha256']}))
