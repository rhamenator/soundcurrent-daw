#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Maintain Qt Linguist catalogs. Runtime/builds use embedded, verified QM files.
--update extracts strings, preserves translator edits and compiles with lrelease.
--check requires no translation tools; it audits inventory, placeholders and hashes.
"""
import argparse,ast,hashlib,json,re,shutil,subprocess,sys
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'data/localization'
LITERAL=r'"(?:\\.|[^"\\])*"'
MESSAGE=re.compile(r'\b(?:SC_TR|text)\(\s*((?:'+LITERAL+r'\s*)+)\)')
PLACEHOLDER=re.compile(r'%L?\d+|%n')
def sources():
    out=set()
    for name in ['main.cpp','equipment_profiles.cpp','enhancement_controls.h','update_panel.h','localization.h','processing_guard.cpp','studio_panel.cpp']:
        p=ROOT/'src'/name
        if not p.exists():continue
        code=p.read_text(encoding="utf-8")
        for m in MESSAGE.finditer(code):out.add(''.join(ast.literal_eval(s) for s in re.findall(LITERAL,m[1])))
    # Data-driven UI labels are translated at the view boundary, keeping IDs fixed.
    out.update(json.loads((DATA/'seed-translations.json').read_text(encoding="utf-8"))['sources'])
    out.update(['Warmth','Boxiness','Clarity','Air'])
    code=(ROOT/'src/enhancement.h').read_text(encoding="utf-8")
    out.update(re.findall(r'\{\s*"([^"]+)"\s*,',code))
    code=(ROOT/'src/main.cpp').read_text(encoding="utf-8")
    start=code.index('void rebuildPresetList(');end=code.index('void savePreset(',start)
    for m in re.finditer(r'addGroup\(\{([^}]+)\}',code[start:end]):out.update(re.findall('"([^"]+)"',m[1]))
    return sorted(out)
def read(path):
    result={}
    if path.exists():
        for m in ET.parse(path).findall('./context/message'):
            source=m.findtext('source');t=m.find('translation')
            if t is not None and t.get('type')!='unfinished':result[source]=t.text or ''
    return result
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def update():
    contexts=json.loads((DATA/'translation-context.json').read_text(encoding='utf-8'))
    strings=sources();seeds=json.loads((DATA/'seed-translations.json').read_text(encoding="utf-8"));meta=[]
    tool=shutil.which('lrelease6')
    if not tool and Path('/usr/lib/qt6/bin/lrelease').exists():tool='/usr/lib/qt6/bin/lrelease'
    if not tool:tool=shutil.which('lrelease')
    if not tool:raise SystemExit('Qt Linguist lrelease is required for --update')
    for tag,row in [('en',['English']+seeds['sources'])]+list(seeds['languages'].items()):
        path=DATA/('soundcurrent_'+tag+'.ts');old=read(path)
        if tag=='en':old={s:s for s in strings}
        else:
            assert len(row)==len(seeds['sources'])+1,(tag,len(row))
            for s,t in zip(seeds['sources'],row[1:]):old.setdefault(s,t)
        root=ET.Element('TS',version='2.1',language=tag.replace('-','_'),sourcelanguage='en_US')
        ctx=ET.SubElement(root,'context');ET.SubElement(ctx,'name').text='SoundCurrent'
        for source in strings:
            msg=ET.SubElement(ctx,'message');ET.SubElement(msg,'source').text=source
            if source in contexts:ET.SubElement(msg,'extracomment').text=contexts[source]
            t=ET.SubElement(msg,'translation');t.text=old.get(source,'')
            if not t.text:t.set('type','unfinished')
        ET.indent(root);ET.ElementTree(root).write(path,encoding='utf-8',xml_declaration=True)
        qm=path.with_suffix('.qm');subprocess.run([tool,'-silent','-nounfinished',str(path),'-qm',str(qm)],check=True)
        done=sum(bool(old.get(s)) for s in strings)
        meta.append({'tag':tag,'name':row[0],'translated':done,'total':len(strings),'status':'source' if tag=='en' else 'unverified','nativeReviewed':False,'tsSha256':digest(path),'qmSha256':digest(qm)})
    (DATA/'catalogs.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n', encoding='utf-8', newline='\n')
    r=ET.Element('RCC');q=ET.SubElement(r,'qresource',prefix='/i18n')
    ET.SubElement(q,'file',alias='catalogs.json').text='catalogs.json'
    for m in meta:ET.SubElement(q,'file',alias='soundcurrent_'+m['tag']+'.qm').text='soundcurrent_'+m['tag']+'.qm'
    ET.indent(r);ET.ElementTree(r).write(DATA/'resources.qrc',encoding='utf-8',xml_declaration=True)
def check():
    strings=set(sources());meta=json.loads((DATA/'catalogs.json').read_text(encoding="utf-8"))
    for item in meta:
        ts=DATA/('soundcurrent_'+item['tag']+'.ts');qm=ts.with_suffix('.qm')
        assert digest(ts)==item['tsSha256'] and digest(qm)==item['qmSha256'],f'Stale QM/TS: {ts}'
        tree=ET.parse(ts);messages=tree.findall('./context/message')
        assert {m.findtext('source') for m in messages}==strings,'Run --update after UI changes'
        assert len(messages)==len(strings),'Duplicate catalog keys'
        done=0
        for m in messages:
            t=m.find('translation');source=m.findtext('source')
            if t.get('type')=='unfinished':continue
            translated=t.text or '';assert translated.strip(),(ts,source)
            assert sorted(PLACEHOLDER.findall(source))==sorted(PLACEHOLDER.findall(translated)),(ts,source,'placeholder mismatch')
            assert source.count('&&')==translated.count('&&'),(ts,source,'literal ampersand mismatch')
            assert not any(c in translated for c in '\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069'),(ts,source,'invisible direction control; use runtime layout')
            done+=1
        assert done==item['translated'] and item['total']==len(strings)
    print(f'PASS: {len(meta)} catalogs, {len(strings)} source messages; placeholders, coverage and compiled catalog hashes')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--update',action='store_true');parser.add_argument('--check',action='store_true');args=parser.parse_args()
    if args.update:update()
    check()
