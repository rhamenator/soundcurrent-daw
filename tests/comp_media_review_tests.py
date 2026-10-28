#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Use immutable owned R3 PCM bank; no rerender, endpoints or model adoption."""
import argparse,hashlib,json,subprocess,tempfile,zipfile
from pathlib import Path,PurePosixPath
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);p.add_argument('--evidence',type=Path,required=True);a=p.parse_args()
archive=Path(__file__).resolve().parent/'results/M2/2026-10-14-comp-edge-assembly/acceptance.zip'
expected='a34df2dc2ff59633f76f982b3c49478ff389f9b42eda9eadcca2b206e53b06b4'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==expected
assert archive.stat().st_size==7863153
assert not a.evidence.is_file();a.evidence.mkdir(parents=True,exist_ok=True);scope=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve();bank=scope/'bank';bank.mkdir()
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for n in z.namelist():
        q=PurePosixPath(n);assert not q.is_absolute() and '..' not in q.parts
        if n.startswith('pcm/') and (n.endswith('.pcm') or n=='pcm/report.json'):
            target=bank/Path(*q.parts[1:]);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(n))
before={str(f.relative_to(bank)):hashlib.sha256(f.read_bytes()).hexdigest() for f in bank.rglob('*') if f.is_file()}
command=[str(a.fixture.resolve()),str(bank),str(scope/'results')]
with (scope/'fixture.stdout').open('w') as out,(scope/'fixture.stderr').open('w') as err:
    process=subprocess.Popen(command,stdout=out,stderr=err);code=process.wait()
receipt={'command':command,'pid':process.pid,'exitCode':code,'ownedSourceArchiveSha256':expected,'bankFiles':before,'freshR3Renders':0,'nativeEndpoints':False}
(scope/'process.json').write_text(json.dumps(receipt,indent=2)+'\n');assert code==0,(scope,code,(scope/'fixture.stderr').read_text())
assert before=={str(f.relative_to(bank)):hashlib.sha256(f.read_bytes()).hexdigest() for f in bank.rglob('*') if f.is_file()}
report=json.loads((scope/'results/report.json').read_text());print(json.dumps({k:report[k] for k in ['checks','physicalFragments','postSrcFragments','physicalSamplesCompared','projectSamplesCompared','qualityAdmission']}));print(scope)
