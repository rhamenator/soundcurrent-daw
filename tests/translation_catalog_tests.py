#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise catalog audit failure paths on retained, isolated input copies."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
owned = Path(tempfile.mkdtemp(prefix='sc-localization-catalogs-'))
print(f'Owned localization catalog fixture root: "{owned}"', flush=True)
spec = importlib.util.spec_from_file_location('catalog_audit', ROOT/'tools/localization.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)

def run(name, mutate=None):
    tree = owned/name
    shutil.copytree(ROOT/'ui', tree/'ui')
    shutil.copytree(ROOT/'localization', tree/'localization')
    audit.ROOT, audit.DATA = tree, tree/'localization'
    if mutate:
        mutate(tree)
    try:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            audit.check()
    except AssertionError as error:
        if not mutate:
            raise
        (tree/'refusal.txt').write_text(str(error)+'\n')
        print(f'PASS: refused {name}')
    else:
        if mutate:
            raise AssertionError(f'Corrupt fixture accepted: {name}')
        (tree/'report.json').write_text(output.getvalue())
        print('PASS: matching retained catalogs')

def change_ts(tree, edit):
    data = tree/'localization'
    ts = data/'soundcurrent_daw_en.ts'
    document = ET.parse(ts)
    edit(document.getroot())
    document.write(ts, encoding='utf-8', xml_declaration=True)
    rows = json.loads((data/'catalogs.json').read_text())
    rows[0]['tsSha256'] = hashlib.sha256(ts.read_bytes()).hexdigest()
    # Refresh the integrity field: this exercises semantics, not only hashing.
    (data/'catalogs.json').write_text(json.dumps(rows))

def finished(tree, predicate=lambda m: True):
    return next(m for _,m in audit.messages(tree)
                if m.find('translation').get('type') != 'unfinished' and predicate(m))

run('matching')
run('stale-ui', lambda tree: (tree/'ui/main.cpp').write_bytes((tree/'ui/main.cpp').read_bytes()+b'\n// isolated change\n'))
run('empty-finished', lambda tree: change_ts(tree, lambda t: setattr(finished(t).find('translation'), 'text', '')))
run('placeholder-loss', lambda tree: change_ts(tree, lambda t: setattr(finished(t,lambda m:'%1' in m.findtext('source')).find('translation'), 'text', 'No parameter')))
run('placeholder-duplication', lambda tree: change_ts(tree, lambda t: setattr(finished(t,lambda m:'%1' in m.findtext('source')).find('translation'), 'text', finished(t,lambda m:'%1' in m.findtext('source')).findtext('source')+' %1')))
run('file-filter-loss', lambda tree: change_ts(tree, lambda t: setattr(finished(t,lambda m:'*.wav' in m.findtext('source')).find('translation'), 'text', 'Audio (*.mp3)')))
run('rich-text-injection', lambda tree: change_ts(tree, lambda t: setattr(finished(t).find('translation'), 'text', '<a href="https://unreviewed.example/">Added link</a>')))
run('hidden-bidi', lambda tree: change_ts(tree, lambda t: setattr(finished(t).find('translation'), 'text', 'Hidden \u202e override')))
run('ampersand-loss', lambda tree: change_ts(tree, lambda t: setattr(finished(t).find('translation'), 'text', 'Added && literal')))
run('duplicate-key', lambda tree: change_ts(tree, lambda t: t.find('context').append(ET.fromstring(ET.tostring(t.find('./context/message'))))))
run('missing-key', lambda tree: change_ts(tree, lambda t: t.find('context').remove(t.find('./context/message'))))

def empty_catalog(tree):
    def edit(t):
        for _,m in audit.messages(t):
            m.find('translation').set('type','unfinished')
    change_ts(tree, edit)
    p=tree/'localization/catalogs.json';rows=json.loads(p.read_text());rows[0]['translated']=0;p.write_text(json.dumps(rows))
run('empty-catalog', empty_catalog)
run('qm-corruption', lambda tree: (tree/'localization/soundcurrent_daw_de.qm').write_bytes(b'corrupt compiled input'))

def resource_alias(tree):
    p=tree/'localization/daw_translations.qrc';t=ET.parse(p)
    t.find('./qresource/file').set('alias','unexpected.json');t.write(p)
run('wrong-resource-alias',resource_alias)

# Retain translator work through the real Qt extraction/compilation cycle.
# An unfinished entry must not be replaced by a newer borrowed draft, and plural
# forms/comments must survive even though they are excluded from runtime QMs.
preserved=owned/'translator-preservation'
shutil.copytree(ROOT/'ui',preserved/'ui')
shutil.copytree(ROOT/'localization',preserved/'localization')
audit.ROOT,audit.DATA=preserved,preserved/'localization'
ts=audit.DATA/'soundcurrent_daw_de.ts'
document=ET.parse(ts);tree=document.getroot()
scalar=next(m for k,m in audit.messages(tree) if k[0]=='StandardActions' and k[1]=='Save')
scalar.find('translation').set('type','unfinished')
scalar.find('translation').text='Eigener noch ungeprüfter Entwurf'
ET.SubElement(scalar,'translatorcomment').text='Translator note: retain pending review.'
plural=next(m for k,m in audit.messages(tree) if k[3])
t=plural.find('translation');t.set('type','unfinished')
ET.SubElement(t,'numerusform').text='%n eigener Eintrag'
ET.SubElement(t,'numerusform').text='%n eigene Einträge'
ET.SubElement(plural,'translatorcomment').text='Plural draft: review separately.'
expected={k:ET.tostring(m.find('translation')) for k,m in audit.messages(tree) if m is scalar or m is plural}
comments={k:[e.text for e in m.findall('translatorcomment')] for k,m in audit.messages(tree) if m is scalar or m is plural}
document.write(ts,encoding='utf-8',xml_declaration=True)
with contextlib.redirect_stdout(io.StringIO()):
    audit.update();audit.check()
after=dict(audit.messages(ET.parse(ts).getroot()))
for k,original in expected.items():
    # XML indentation is presentation; compare semantic attributes/children.
    a,b=ET.fromstring(original),after[k].find('translation')
    assert a.attrib==b.attrib and (a.text or '').strip()==(b.text or '').strip()
    assert [(e.tag,e.text,e.attrib) for e in a]==[(e.tag,e.text,e.attrib) for e in b]
    assert [e.text for e in after[k].findall('translatorcomment')]==comments[k]
print('PASS: actual Linguist update retained unfinished scalar/plural drafts and translator comments.')
print('PASS: 13 isolated corruption/refusal workflows; originals untouched; no native language qualification.')
