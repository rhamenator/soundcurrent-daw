#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deduplicate qualified file evidence; preserve every original payload byte."""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,zipfile
p=argparse.ArgumentParser();p.add_argument('original',type=Path);p.add_argument('original_index',type=Path);p.add_argument('destination',type=Path);a=p.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
index=json.loads(a.original_index.read_text());old=index['archive'];assert a.original.stat().st_size==old['bytes'] and sha(a.original.read_bytes())==old['sha256']
manifest={};seen=set();a.destination.mkdir(parents=True,exist_ok=True);archive=a.destination/'acceptance.zip';assert not archive.exists()
with zipfile.ZipFile(a.original) as source,zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    assert source.testzip() is None and len(source.namelist())==len(set(source.namelist()))
    for name in sorted(source.namelist()):
        q=PurePosixPath(name);assert not q.is_absolute() and '..' not in q.parts and '\\' not in name
        data=source.read(name);digest=sha(data);assert old['entries'][name]=={'bytes':len(data),'sha256':digest}
        blob='blobs/'+digest;manifest[name]={'bytes':len(data),'sha256':digest,'blob':blob}
        if blob not in seen:out.writestr(blob,data);seen.add(blob)
    out.writestr('files.json',json.dumps(manifest,indent=2)+'\n')
    out.writestr('original-archive.json',json.dumps({'bytes':old['bytes'],'sha256':old['sha256'],'entries':len(manifest)},indent=2)+'\n')
with zipfile.ZipFile(archive) as packed,zipfile.ZipFile(a.original) as original:
    assert packed.testzip() is None
    assert json.loads(packed.read('files.json'))==manifest
    for name,entry in manifest.items():
        data=packed.read(entry['blob']);assert len(data)==entry['bytes'] and sha(data)==entry['sha256'] and data==original.read(name)
result={k:v for k,v in index.items() if k!='archive'}
result['archive']={'bytes':archive.stat().st_size,'sha256':sha(archive.read_bytes()),'entries':len(seen)+2,'originalFiles':len(manifest),'uniquePayloads':len(seen),'format':'sc-hash-file-evidence-v1','fileManifest':'files.json','originalArchive':{'bytes':old['bytes'],'sha256':old['sha256']},'allOriginalPayloadsByteIdentical':True}
(a.destination/'index.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['archive']))
