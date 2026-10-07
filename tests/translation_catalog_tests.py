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
print('PASS: 10 isolated corruption/refusal workflows; originals untouched; no native language qualification.')
