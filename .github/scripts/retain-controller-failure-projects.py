# SPDX-License-Identifier: GPL-3.0-only
"""Retain bounded generated controller failures; never an atomic live snapshot."""
import argparse, hashlib, json, os, re, stat, tempfile
from pathlib import Path
MARKER = 'Retained PipeWire controller fixture '
PANEL_MARKER = 'Retained native panel fixture '
OWNER_MARKER = 'Retained owner fixture '
POLICIES = {MARKER:'sc-pw-controller-é-',PANEL_MARKER:'sc-native-panel-é-',OWNER_MARKER:'sc-pw-owner-é-'}
UUID = re.compile(r'^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$')
def plain(path, directory=False):
    s=path.lstat()
    if stat.S_ISLNK(s.st_mode) or getattr(s,'st_file_attributes',0)&0x400:
        raise ValueError('Linked/reparse retention path refused')
    if not (stat.S_ISDIR(s.st_mode) if directory else stat.S_ISREG(s.st_mode)):
        raise ValueError('Nonplain retention path refused')
    return s

def retain(build):
    build=Path(os.path.abspath(build));plain(build,True)
    logs=build/'Testing'/'Temporary';sources=set()
    if not logs.exists():return []
    plain(build/'Testing',True);plain(logs,True)
    inputs=list(logs.glob('*.log'))
    if len(inputs)>16:raise ValueError('Retention log count bound exceeded')
    for p in sorted(inputs):
        if plain(p).st_size>16*1024*1024:raise ValueError('Retention log bound exceeded')
        for line in p.read_text(encoding='utf-8',errors='strict').splitlines():
            marker=next((m for m in POLICIES if line.startswith(m)),None)
            if marker is None:continue
            raw=line[len(marker):];source=Path(os.path.abspath(raw))
            parent=Path(tempfile.gettempdir())/POLICIES[marker]
            if source.parent!=parent or not UUID.fullmatch(source.name):raise ValueError('Foreign controller failure path refused')
            plain(parent,True);plain(source,True);sources.add(source)
            if len(sources)>3:raise ValueError('Controller failure count bound exceeded')
    destination=build/'controller-failure-projects'
    if destination.exists():plain(destination,True)
    receipts=[]
    for source in sorted(sources):
        queue=[source];files=[];entries=0;size=0
        while queue:
            folder=queue.pop();plain(folder,True)
            for item in sorted(folder.iterdir()):
                entries+=1
                if entries>512:raise ValueError('Controller entry bound exceeded')
                info=item.lstat()
                if stat.S_ISDIR(info.st_mode):plain(item,True);queue.append(item)
                else:
                    plain(item);size+=info.st_size
                    if size>512*1024*1024:raise ValueError('Controller byte bound exceeded')
                    files.append((item,item.relative_to(source),info.st_size))
        prefix={'sc-native-panel-é-':'panel-','sc-pw-owner-é-':'owner-','sc-pw-controller-é-':'controller-'}[source.parent.name]
        target=destination/(prefix+source.name)
        if target.exists():raise ValueError('Existing controller snapshot refused')
        target.mkdir(parents=True);manifest=[]
        for item,relative,length in files:
            plain(item);out=target/relative;out.parent.mkdir(parents=True,exist_ok=True)
            with item.open('rb') as inp:before=hashlib.file_digest(inp,'sha256').hexdigest()
            with item.open('rb') as inp,out.open('xb') as output:
                remaining=length
                while remaining:
                    block=inp.read(min(65536,remaining))
                    if not block:raise ValueError('Controller source truncated')
                    output.write(block);remaining-=len(block)
                if inp.read(1):raise ValueError('Controller source grew')
            with item.open('rb') as inp,out.open('rb') as copied:
                after=hashlib.file_digest(inp,'sha256').hexdigest();digest=hashlib.file_digest(copied,'sha256').hexdigest()
            if before!=after or before!=digest or plain(item).st_size!=length:raise ValueError('Controller bytes changed during retention')
            manifest.append({'path':relative.as_posix(),'bytes':length,'sha256':digest})
        receipt={'schema':1,'source':str(source),'totalBytes':size,'originalFiles':manifest,'atomicProjectSnapshot':False}
        (target/'retention.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');receipts.append(receipt)
    return receipts
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('build');a=p.parse_args();print(json.dumps(retain(a.build),ensure_ascii=False))
