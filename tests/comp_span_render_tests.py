#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('fixture',type=Path);p.add_argument('evidence',type=Path);a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=True);root=Path(tempfile.mkdtemp(prefix='run-',dir=a.evidence)).resolve()
command=[str(a.fixture.resolve()),str(root/'pcm')]
with (root/'stdout').open('w') as out,(root/'stderr').open('w') as err:
    process=subprocess.Popen(command,stdout=out,stderr=err);code=process.wait()
(root/'process.json').write_text(json.dumps({'command':command,'pid':process.pid,'exitCode':code},indent=2)+'\n')
assert code==0,(root,code,(root/'stderr').read_text());print((root/'stdout').read_text());print(root)
