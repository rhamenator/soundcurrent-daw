#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check bounded retained CI observations, without Git/code/audio replay."""
import hashlib,io,json,re,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent;s=json.loads((root/'summary.json').read_bytes());raw=(root/'job.zip').read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
assert len(raw)==s['archiveBytes']<1024*1024 and sha(raw)==s['archiveSha256']
with zipfile.ZipFile(io.BytesIO(raw))as z:
 assert z.namelist()==['windows-desktop-job.txt'] and z.infolist()[0].file_size<1024*1024
 log=z.read('windows-desktop-job.txt')
assert len(log)==s['logBytes'] and sha(log)==s['logSha256'];text=log.decode('utf-8-sig')
assert (s['head'],s['base'],s['testedMerge'])==('61c6918aa561998f18d81a89e1e4d58b55935219','92b7355e049fcd87018d34980b156356cbd883d7','bf0a22236c7399f6d5b47c7a5cd2a29750092e86')
assert 'Merge '+s['head']+' into '+s['base'] in text and s['testedMerge']in text
assert 'desktop_loop_checks=15 saved_undo_reopen=true raw_unchanged=true prepared_loop_until_stop=true source_fallback=true native_audio=false'in text
assert '16/18 Test #53: desktop-loop-controls' in text and '100% tests passed out of 18'in text
assert (s['run'],s['job'],s['ctestSuites'],s['loopChecks'],s['linuxScreenshotCheckExtra'],s['conclusion'])==(38106724888,114373711318,18,15,1,'success')
assert len(s['changedBlobs'])==70
for n,v in s['changedBlobs'].items():assert not n.startswith('/')and'..'not in n.split('/')and re.fullmatch('[0-9a-f]{40}',v)
for n in ('desktopLoopControllerQualified','simulatedAudioEndpoint'):assert s[n]is True
for n in ('nativeAudioQualified','installedPreviewQualified','M2Complete','fullGoalComplete','productArtifactUploaded','currentMainMergeAdmission'):assert s[n]is False
print('Retained Windows CI observations checked: 18 suites, 15 loop GUI/controller checks with simulated audio. Source/executables/audio not replayed; no signature, physical endpoint or current-main merge admission claim.')
