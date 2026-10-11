#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate a retained Windows CI observation; optional changed-file comparison.
No historical Git requirement, native audio/UI replay or signature claim.
"""
import argparse,hashlib,io,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-dir',type=Path);a=p.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
s=json.loads((ROOT/'summary.json').read_bytes());m=json.loads((ROOT/'members.json').read_bytes());raw=(ROOT/'capture.zip').read_bytes()
assert len(raw)==s['captureBytes']<1024*1024 and sha(raw)==s['captureSha256']
expected={'windows-job.txt','tested-inputs.json','job-status.json'}
with zipfile.ZipFile(io.BytesIO(raw))as z:
 assert len(z.namelist())==len(set(z.namelist()))==3 and set(z.namelist())==set(m)==expected
 assert sum(i.file_size for i in z.infolist())<2*1024*1024 and all(i.file_size<1024*1024 for i in z.infolist())
 b={n:z.read(n)for n in expected}
for n,v in b.items():assert m[n]=={'bytes':len(v),'sha256':sha(v)}
i=json.loads(b['tested-inputs.json']);j=json.loads(b['job-status.json']);log=b['windows-job.txt']
assert i['head']==s['head']=='01b0dc8d711617d5264e7323034c332d0662b5aa'
assert i['base']==s['base']=='aab9ca5b15f422ea112e72f7fdadc9068d29e644'
assert i['testedMerge']==s['testedMerge']=='473db99da4a0d1bf0f220e3e0af3aab4533d3c2a' and i['tree']==s['testedTree']
assert len(i['changedFiles'])==s['reviewedChangedFiles']==59
for n,h in i['changedFiles'].items():
 assert not n.startswith('/') and '..'not in n.split('/') and re.fullmatch('[0-9a-f]{40}',h['gitBlob']) and re.fullmatch('[0-9a-f]{64}',h['sha256'])
 if a.source_dir:assert sha((a.source_dir/n).read_bytes())==h['sha256'],n
assert j['id']==s['jobId']==114385491597 and j['run_id']==s['runId']==38110705676 and j['status']=='completed' and j['conclusion']=='success'
assert j['name']=='Windows desktop import and localization tests'
assert i['testedMerge'].encode()in log and ('Merge '+i['head']+' into '+i['base']).encode()in log
assert b'desktop_loop_recording_checks=36 actual_passes=13 partial_explicit=true original_context_verified=true scalar_rebase=true raw_unchanged=true save_reopen=true native_audio=false'in log
assert b'100% tests passed out of 19'in log and b'Total Test time (real) =  66.24 sec'in log and b'windows-2025'in log
assert s['windowsDesktopSuites']==19 and s['desktopLoopWorkflowChecks']==36 and s['wallSeconds']==66.24 and s['syntheticWindowsDesktopQualified']is True
for n in ('nativeWindowsDuplexQualified','installedPreviewQualified','fullLanguageQualified','M2Complete','fullGoalComplete','productArtifactUploaded'):assert s[n]is False
print('Windows desktop loop CI packet checked:19 suites/36 GUI checks on Windows OS with simulated endpoints. '+('59 changed-file bytes compared. 'if a.source_dir else'Changed-file bytes not compared. ')+'No audio/UI replay, signature, native duplex or full-language qualification.')
