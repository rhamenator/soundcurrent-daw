#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit compact observation/source provenance, not acoustic recomputation."""
from pathlib import Path
import hashlib
import io
import json
import math
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent
REPO = next(p for p in ROOT.parents if (p/'.git').exists())
HEAD = '20a062dbf8449cd6e6c2b167b2ed12a3320e46f1'
RETRY = '8058778a01cffe16b2c4a3ab252c6613fbf63ee5'
sha = lambda b: hashlib.sha256(b).hexdigest()
git = lambda *a: subprocess.check_output(['git', *a], cwd=REPO)
s = json.loads((ROOT/'summary.json').read_bytes())
raw = (ROOT/'observations.zip').read_bytes()
assert len(raw) == s['captureBytes'] and sha(raw) == s['captureSha256']
members = json.loads((ROOT/'members.json').read_bytes())
expected = {'contract.json', 'inputs.json', 'originals.json', 'processes.json', 'results.json', 'summary.json', 'configure.txt', 'build.txt', 'run-original.txt', 'parent-retry.txt', 'parent-retry-receipt.json'}
with zipfile.ZipFile(io.BytesIO(raw)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == len(expected) == 11
    assert set(z.namelist()) == expected == set(members)
    assert sum(i.file_size for i in z.infolist()) < 1024*1024
    assert all('/' not in i.filename and i.file_size < 512*1024 for i in z.infolist())
    b = {n: z.read(n) for n in expected}
for n, data in b.items():
    assert members[n] == {'bytes': len(data), 'sha256': sha(data)}
    assert not data.startswith((b'MZ', b'\x7fELF', b'RIFF', b'RF64'))
i = json.loads(b['inputs.json'])
assert i['sourceCommit'] == s['sourceCommit'] == HEAD
assert git('rev-parse', HEAD+'^{tree}').decode().strip() == i['sourceTree']
paths = {p for p in git('ls-tree', '-r', '--name-only', HEAD, '--', 'include', 'src', 'ui', 'worker', 'tests', 'CMakeLists.txt').decode().splitlines() if p.endswith(('.cpp', '.hpp', '.h', '.c')) or p == 'CMakeLists.txt'}
assert paths == set(i['files']) and len(paths) == s['sourceInputs'] == 309
for p in paths:
    assert i['files'][p] == sha(git('show', HEAD+':'+p))
    assert i['files'][p] == sha(git('show', RETRY+':'+p))
assert i['runnerSha256'] == sha(git('show', HEAD+':experiments/stretch-rate-quality/run.py'))
frozen_contract = git('show', HEAD+':experiments/stretch-rate-quality/contract.json')
assert i['contractSha256'] == sha(frozen_contract)
assert json.loads(b['contract.json']) == json.loads(frozen_contract)
assert len(i['workerSha256']) == len(i['verifierSha256']) == 64
assert b'Linking CXX executable sc-stretch-render-worker' in b['build.txt']
assert b'Linking CXX executable sc-stretch-artifact-tests' in b['build.txt']
c = json.loads(b['contract.json'])
originals = json.loads(b['originals.json'])
assert {o['rate'] for o in originals} == {44100, 48000, 96000}
assert all(o['frames'] == 2*o['rate']+1 and len(o['sha256']) == 64 for o in originals)
assert all(abs(f['errorCents']) <= 5 for o in originals for f in o['calibration'])
rows = json.loads(b['results.json'])
processes = json.loads(b['processes.json'])
assert len(rows) == 36
assert {(r['rate'], *r['ratio'], r['pitchMilliCents']) for r in rows} == {(rate, n, d, pitch) for rate in c['rates'] for n, d in c['ratios'] for pitch in c['pitchMilliCents']}
errors = []
for r in rows:
    assert r['complete'] is True and r['floatHeadroom'] is True and r['peak'] > 1
    req = r['request']
    n, d = r['ratio']
    assert req['rate'] == r['rate'] and req['pitchMilliCents'] == r['pitchMilliCents'] and [req['timeNumerator'], req['timeDenominator']] == [n, d]
    assert req['sourceFrames'] == req['frames'] == 2*r['rate']+1
    assert r['frames'] == (req['frames']*n+d-1)//d
    assert req['sha256'] == next(o['sha256'] for o in originals if o['rate'] == r['rate'])
    p = processes[r['processIndex']]
    assert p['kind'] == 'worker' and p['request'] == req and p['exitCode'] == 0
    assert p['observationTimedOut'] is False and p['outputLimited'] is False and p['retirementPending'] is False
    ready, receipt = map(json.loads, p['stdout'].splitlines())
    assert ready['operation'] == receipt['operation'] == req['operation'] and receipt['complete'] is True
    assert receipt['writtenFrames'] == r['frames'] and receipt['audioSha256'] == r['audioSha256'] and receipt['sampleSha256'] == r['sampleSha256']
    assert len(r['frequency']) == 2
    for ch, o in enumerate(r['frequency']):
        target = [1000., 1500.][ch]*2**(r['pitchMilliCents']/1200000)
        assert o['channel'] == ch and o['expectedHz'] == target
        assert math.isclose(o['errorCents'], 1200*math.log2(o['measuredHz']/target), abs_tol=1e-10)
        assert o['withinFiveCents'] is True and abs(o['errorCents']) <= 5
        errors.append(abs(o['errorCents']))
assert sum(r['unityExact'] for r in rows) == 3
assert s['completedRenders'] == s['attemptedRenders'] == 36 and s['frequencyObservations'] == s['frequencyPasses'] == len(errors) == 72
assert s['maximumFrequencyErrorCents'] == max(errors)
parents = [p for p in processes if p['kind'] == 'parent-verifier']
assert len(parents) == 4 and sum(p['exitCode'] == 0 for p in parents) == 3
failed = next(p for p in parents if p['exitCode'] != 0)
assert failed['stderr'] == 'Refusing to overwrite another project\n'
retry = json.loads(b['parent-retry-receipt.json'])
assert (retry['originalSource'], retry['retrySource']) == (HEAD, RETRY)
assert retry['originalOperation'] == failed['operation'] and retry['exitCode'] == 0 and retry['stderr'] == ''
failed_rows = [r for r in rows if r['request']['operation'] == failed['operation']]
assert len(failed_rows) == 1
failed_row = failed_rows[0]
assert retry['request'] == failed_row['request']
copied = retry['copiedInputsOriginalsUnchanged']
operation_path = 'media/derived/' + failed['operation'] + '/'
assert set(copied) == {failed_row['request']['relative']} | {operation_path+n for n in ('audio.wav', 'complete.json', 'intent.json')}
assert copied[operation_path+'audio.wav']['sha256'] == failed_row['audioSha256']
assert copied[failed_row['request']['relative']]['sha256'] == failed_row['request']['sha256']
assert copied[failed_row['request']['relative']]['bytes'] == 44 + 8*failed_row['request']['sourceFrames']
assert all(0 < record['bytes'] <= 4194304 and len(record['sha256']) == 64 for record in copied.values())
assert retry['verifierSha256'] == i['verifierSha256']
assert 'derived_shared_live_export_checks=7919 callback_audit=0 seek_split_crop_equal=true save_reopen=true' in retry['stdout']
assert retry['rerenderedAudio'] is False and retry['originalBankChanged'] is False
assert s['publicPcmIncluded'] is False and s['publicSpectralRecomputationQualified'] is False
assert all(s[k] is False for k in c['scope']) and all(v is False for v in c['scope'].values())
print('Compact Linux quality observations: 36 completed renders, 72 within-five-cent measurements, original 3/4 parent workflows plus unchanged-audio retry; source/protocol audit only, no PCM/acoustic recomputation')
