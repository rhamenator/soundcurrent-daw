#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DAW Qt Linguist catalog maintenance. Adapted from the equalizer script.
--update uses real lupdate/lrelease. --check is a dependency-free structural audit.
Draft terms are reused only for identical source text in the DAW's own contexts.
"""
import argparse, hashlib, json, re, shutil, subprocess, tempfile
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'localization'
PLACEHOLDER = re.compile(r'%L?(?:[0-9]+|n)')
BIDI = '\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def sources():
    return sorted(p for p in (ROOT/'ui').iterdir() if p.suffix in {'.cpp','.hpp'})
def key(context, message):
    return (context, message.findtext('source'), message.findtext('comment') or '', message.get('numerus')=='yes')
def messages(tree):
    for c in tree.findall('context'):
        for m in c.findall('message'): yield key(c.findtext('name'),m),m
def tool(name):
    p=Path('/usr/lib/qt6/bin')/name
    return str(p) if p.exists() else shutil.which(name+'6') or shutil.which(name)
def update():
    updater,compiler=tool('lupdate'),tool('lrelease')
    if not updater or not compiler: raise SystemExit('Qt 6 Linguist lupdate/lrelease required only for --update')
    source_files=sources();seeds=json.loads((DATA/'seed-translations.json').read_text());notes=json.loads((DATA/'translation-context.json').read_text());meta=[]
    with tempfile.TemporaryDirectory(prefix='sc-daw-translations-') as temporary:
        prototype=Path(temporary)/'source.ts'
        subprocess.run([updater,*map(str,source_files),'-no-obsolete','-locations','none','-ts',str(prototype)],check=True)
        raw=ET.parse(prototype)
        for tag,row in [('en',['English']+seeds['sources'])]+list(seeds['languages'].items()):
            assert len(row)==len(seeds['sources'])+1
            ts=DATA/('soundcurrent_daw_'+tag+'.ts');old={}
            if ts.exists():
                old={k:m.find('translation') for k,m in messages(ET.parse(ts).getroot())}
            tree=ET.fromstring(ET.tostring(raw.getroot()));tree.set('language',tag.replace('-','_'));tree.set('sourcelanguage','en_US')
            seed=dict(zip(seeds['sources'],row[1:]));done=0
            for k,m in messages(tree):
                source=k[1];m.remove(m.find('translation'))
                # Preserve translator edits, unfinished entries, comments and plural forms.
                previous=old.get(k)
                if tag=='en' and not k[3]:
                    t=ET.SubElement(m,'translation');t.text=source
                elif previous is not None:
                    t=ET.fromstring(ET.tostring(previous));m.append(t)
                else:
                    t=ET.SubElement(m,'translation')
                    if not k[3] and source in seed: t.text=seed[source]
                    else: t.set('type','unfinished')
                if source in notes: ET.SubElement(m,'extracomment').text=notes[source]
                if t.get('type')!='unfinished': done+=1
            ET.indent(tree);ET.ElementTree(tree).write(ts,encoding='utf-8',xml_declaration=True)
            qm=ts.with_suffix('.qm');subprocess.run([compiler,'-silent','-nounfinished',str(ts),'-qm',str(qm)],check=True)
            meta.append({'tag':tag,'name':row[0],'translated':done,'total':sum(1 for _ in messages(tree)),
                'status':'source' if tag=='en' else 'partial-unverified','nativeReviewed':False,'uiQualified':False,'tsSha256':digest(ts),'qmSha256':digest(qm)})
    (DATA/'catalogs.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    qrc=ET.Element('RCC');q=ET.SubElement(qrc,'qresource',prefix='/daw/i18n');ET.SubElement(q,'file',alias='catalogs.json').text='catalogs.json'
    for item in meta:
        name='soundcurrent_daw_'+item['tag']+'.qm';ET.SubElement(q,'file',alias=name).text=name
    ET.indent(qrc);ET.ElementTree(qrc).write(DATA/'daw_translations.qrc',encoding='utf-8',xml_declaration=True)
    inventory={'source_sha256':{str(p.relative_to(ROOT)):digest(p) for p in source_files},
        'seed_sha256':digest(DATA/'seed-translations.json'),'translator_notes_sha256':digest(DATA/'translation-context.json'),
        'tools':{name:subprocess.check_output([program,'-version'],text=True).strip() for name,program in [('lupdate',updater),('lrelease',compiler)]},
        'keys':[list(k) for k,_ in messages(ET.parse(prototype).getroot())] if prototype.exists() else [list(k) for k,_ in messages(ET.parse(DATA/'soundcurrent_daw_en.ts').getroot())]}
    (DATA/'source-inventory.json').write_text(json.dumps(inventory,ensure_ascii=False,indent=2)+'\n')
def check():
    inventory=json.loads((DATA/'source-inventory.json').read_text());actual={str(p.relative_to(ROOT)):digest(p) for p in sources()}
    assert actual==inventory['source_sha256'],'UI changed: run tools/localization.py --update'
    assert digest(DATA/'seed-translations.json')==inventory['seed_sha256']
    assert digest(DATA/'translation-context.json')==inventory['translator_notes_sha256']
    expected={tuple(k) for k in inventory['keys']};meta=json.loads((DATA/'catalogs.json').read_text());seen=set()
    for item in meta:
        tag=item['tag'];assert re.fullmatch(r'[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*',tag) and tag not in seen;seen.add(tag)
        ts=DATA/('soundcurrent_daw_'+tag+'.ts');assert digest(ts)==item['tsSha256'] and digest(ts.with_suffix('.qm'))==item['qmSha256']
        found=list(messages(ET.parse(ts).getroot()));assert {k for k,_ in found}==expected and len(found)==len(expected),'Context/key mismatch or duplicates'
        done=0
        for k,m in found:
            t=m.find('translation');assert t is not None
            if t.get('type')=='unfinished': continue
            entries=[e.text or '' for e in t.findall('numerusform')] if k[3] else [t.text or '']
            assert entries,'Empty plural forms'
            for text in entries:
                assert text.strip(),(tag,k,'Empty finished entry')
                assert sorted(PLACEHOLDER.findall(text))==sorted(PLACEHOLDER.findall(k[1])),(tag,k,'Placeholder mismatch')
                assert text.count('&&')==k[1].count('&&'),(tag,k,'Literal ampersand mismatch')
                assert not any(c in text for c in BIDI),(tag,k,'Hidden bidi control')
            done+=1
        assert done==item['translated'] and len(expected)==item['total']
        assert done>0,'An empty catalog cannot be exposed as a language'
        assert item['nativeReviewed'] is False and item['uiQualified'] is False,'Promotion requires separate evidence'
        assert item['status']==('source' if tag=='en' else 'partial-unverified')
    files=ET.parse(DATA/'daw_translations.qrc').findall('./qresource/file')
    assert {p.get('alias') for p in files}=={'catalogs.json'}|{'soundcurrent_daw_'+tag+'.qm' for tag in seen}
    assert all(p.text==p.get('alias') for p in files)
    print(json.dumps({'catalogs':len(meta),'source_keys':len(expected),'draft_translations':sum(i['translated'] for i in meta if i['tag']!='en'),'native_reviewed':0,'fully_UI_qualified':0}))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--update',action='store_true');parser.add_argument('--check',action='store_true');args=parser.parse_args()
    if args.update:update()
    check()
