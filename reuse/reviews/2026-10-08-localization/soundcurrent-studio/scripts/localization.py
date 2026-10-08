#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Maintain embedded Qt catalogs without losing unfinished translator work.

--check is structural validation, not linguistic certification. Numerus messages
are rejected until language-specific Qt plural rules are explicitly supported.
"""
import argparse
import ast
import copy
import hashlib
import json
import re
import shutil
import subprocess
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / 'data/localization'
LITERAL = r'"(?:\\.|[^"\\])*"'
PLACEHOLDER = re.compile(r'%L?[1-9][0-9]?|%Ln|%n')
UI_SOURCES = ['main.cpp', 'equipment_profiles.cpp', 'enhancement_controls.h',
              'update_panel.h', 'startup_controls.h', 'localization.h', 'processing_guard.cpp', 'studio_panel.cpp']


def literal(expression):
    expression = expression.strip()
    if not re.fullmatch(r'(?:' + LITERAL + r'\s*)+', expression):
        return None
    return ''.join(ast.literal_eval(s) for s in re.findall(LITERAL, expression))


def calls(code, name):
    """Extract call arguments; skip comments/strings and track nested brackets."""
    tokens = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|' + LITERAL + r"|'(?:\\.|[^'\\])*'|\b[A-Za-z_]\w*\b|[^\s]")
    parts = list(tokens.finditer(code))
    for i, token in enumerate(parts):
        if token.group() != name or i + 1 >= len(parts) or parts[i + 1].group() != '(':
            continue
        depth = 0
        start = parts[i + 1].end()
        arguments = []
        for part in parts[i + 2:]:
            value = part.group()
            if value in ('(', '[', '{'):
                depth += 1
            elif value in (')', ']', '}'):
                if depth == 0:
                    arguments.append(code[start:part.start()].strip())
                    yield arguments
                    break
                depth -= 1
            elif value == ',' and depth == 0:
                arguments.append(code[start:part.start()].strip())
                start = part.end()


def marked_sources(directory):
    """Discover explicit translation markers in every C++ source/header/adapter."""
    out = set()
    for path in sorted(directory.rglob('*')):
        if path.suffix not in ('.cpp', '.h', '.hpp', '.inc'):
            continue
        for args in calls(path.read_text(encoding='utf-8'), 'SC_TR'):
            value = literal(args[0]) if args else None
            if value is not None:
                if len(args) > 1 and args[1] != '-1':
                    raise ValueError('Numerus extraction needs explicit Qt language plural rules')
                out.add(value)
    return out


def check_platform_errors(code):
    """Qt adapter errors are user-facing; backend diagnostics are separate."""
    for args in calls(code, 'runtime_error'):
        if args and literal(args[0]) is not None:
            raise ValueError('Unmarked Windows adapter error: ' + literal(args[0]))


def sources():
    adapter = ROOT / 'src/windows_platform.inc'
    if adapter.exists():
        check_platform_errors(adapter.read_text(encoding='utf-8'))
    out = marked_sources(ROOT / 'src')
    for name in UI_SOURCES:
        path = ROOT / 'src' / name
        if not path.exists():
            continue
        code = path.read_text(encoding='utf-8')
        # Obvious view literals must be wrapped; machine names/units are explicit exceptions.
        for method, argument in [('setWindowTitle', 0), ('setAccessibleName', 0), ('setToolTip', 0),
                                 ('setPlaceholderText', 0), ('setInformativeText', 0),
                                 ('finish', 0), ('addButton', 0), ('addTab', 1), ('insertTab', 2), ('button', 0)]:
            for args in calls(code, method):
                if len(args) <= argument:
                    continue
                value = literal(args[argument])
                if value is not None and value not in ('', 'Q', 'Hz', 'dB', 'SoundCurrent EQ', 'SoundCurrent Studio'):
                    raise ValueError(f'Unmarked UI literal in {name}: {method}: {value}')
        for method in ('SC_TR', 'text'):
            for args in calls(code, method):
                value = literal(args[0]) if args else None
                if value is not None:
                    if len(args) > 1 and args[1] != '-1':
                        raise ValueError('Numerus extraction needs explicit Qt language plural rules')
                    out.add(value)
        # The equipment validation helper translates its literal reason at runtime.
        if name == 'equipment_profiles.cpp':
            for args in calls(code, 'require'):
                if len(args) == 2 and (value := literal(args[1])) is not None:
                    out.add(value)
    out.update(json.loads((DATA / 'seed-translations.json').read_text(encoding='utf-8'))['sources'])
    out.update(['Warmth', 'Boxiness', 'Clarity', 'Air'])
    code = (ROOT / 'src/enhancement.h').read_text(encoding='utf-8')
    out.update(re.findall(r'\{\s*"([^"]+)"\s*,', code))
    code = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
    start = code.index('void rebuildPresetList(')
    end = code.index('void savePreset(', start)
    for args in calls(code[start:end], 'addGroup'):
        out.update(re.findall(r'"([^"]+)"', args[0]))
    return sorted(out)


def entries(path):
    result = {}
    if path.exists():
        for message in ET.parse(path).findall('./context/message'):
            source = message.findtext('source')
            if source in result:
                raise ValueError(f'Duplicate source key: {path}: {source}')
            if message.get('numerus') == 'yes' or message.find('./translation/numerusform') is not None:
                raise ValueError(f'Numerus messages require explicit plural support: {path}: {source}')
            result[source] = copy.deepcopy(message)
    return result


def finished(message):
    translation = message.find('translation')
    return translation is not None and translation.get('type') not in ('unfinished', 'vanished', 'obsolete') and bool((translation.text or '').strip())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Markup(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.structure = []

    def handle_starttag(self, tag, attrs):
        self.structure.append(('start', tag, tuple(sorted(attrs))))

    def handle_endtag(self, tag):
        self.structure.append(('end', tag))

    def handle_startendtag(self, tag, attrs):
        self.structure.append(('empty', tag, tuple(sorted(attrs))))


def validate_text(source, translated):
    if not translated.strip():
        raise ValueError('Blank finished translation')
    if Counter(PLACEHOLDER.findall(source)) != Counter(PLACEHOLDER.findall(translated)):
        raise ValueError('Placeholder mismatch')
    if source.count('&&') != translated.count('&&'):
        raise ValueError('Literal ampersand mismatch')
    if any(c in translated for c in '\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069'):
        raise ValueError('Hidden direction control; use runtime layout')
    original, target = Markup(), Markup()
    original.feed(source)
    target.feed(translated)
    if original.structure != target.structure:
        raise ValueError('Rich-text tags, links or attributes changed')
    # File-dialog filters must preserve extension/glob patterns and filter count.
    if re.search(r'\(\*\.[^)]*\)', source):
        patterns = lambda text: re.findall(r'\(\*\.[^)]*\)', text)
        if patterns(source) != patterns(translated) or source.count(';;') != translated.count(';;'):
            raise ValueError('File-dialog filter patterns changed')


def update():
    contexts = json.loads((DATA / 'translation-context.json').read_text(encoding='utf-8'))
    strings = sources()
    seeds = json.loads((DATA / 'seed-translations.json').read_text(encoding='utf-8'))
    metadata = []
    tool = shutil.which('lrelease6')
    if not tool and Path('/usr/lib/qt6/bin/lrelease').exists():
        tool = '/usr/lib/qt6/bin/lrelease'
    tool = tool or shutil.which('lrelease')
    if not tool:
        raise SystemExit('Qt Linguist lrelease is required for --update')
    rows = [('en', ['English'] + seeds['sources'])] + list(seeds['languages'].items())
    # Parse all catalogs before writing: unsupported numerus must never be erased.
    existing = {tag: entries(DATA / f'soundcurrent_{tag}.ts') for tag, _ in rows}
    for tag, row in rows:
        if len(row) != len(seeds['sources']) + 1:
            raise ValueError(f'Seed row length mismatch: {tag}')
        seed = dict(zip(seeds['sources'], row[1:]))
        root = ET.Element('TS', version='2.1', language=tag.replace('-', '_'), sourcelanguage='en_US')
        context = ET.SubElement(root, 'context')
        ET.SubElement(context, 'name').text = 'SoundCurrent'
        done = 0
        for source in strings:
            message = existing[tag].get(source)
            if message is None:
                message = ET.Element('message')
                ET.SubElement(message, 'source').text = source
                translation = ET.SubElement(message, 'translation')
                translation.text = source if tag == 'en' else seed.get(source, '')
                if not translation.text:
                    translation.set('type', 'unfinished')
            elif tag == 'en':
                translation = message.find('translation')
                if translation is None:
                    translation = ET.SubElement(message, 'translation')
                translation.text = source
                translation.attrib.clear()
            if source in contexts:
                note = message.find('extracomment')
                if note is None:
                    note = ET.SubElement(message, 'extracomment')
                note.text = contexts[source]
            if finished(message):
                validate_text(source, message.findtext('translation'))
                done += 1
            context.append(message)
        path = DATA / f'soundcurrent_{tag}.ts'
        ET.indent(root)
        ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)
        qm = path.with_suffix('.qm')
        subprocess.run([tool, '-silent', '-nounfinished', str(path), '-qm', str(qm)], check=True)
        metadata.append({'tag': tag, 'name': row[0], 'translated': done, 'total': len(strings),
                         'status': 'source' if tag == 'en' else 'unverified', 'nativeReviewed': False,
                         'tsSha256': digest(path), 'qmSha256': digest(qm)})
    (DATA / 'catalogs.json').write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
    resource = ET.Element('RCC')
    group = ET.SubElement(resource, 'qresource', prefix='/i18n')
    ET.SubElement(group, 'file', alias='catalogs.json').text = 'catalogs.json'
    for item in metadata:
        name = f"soundcurrent_{item['tag']}.qm"
        ET.SubElement(group, 'file', alias=name).text = name
    ET.indent(resource)
    ET.ElementTree(resource).write(DATA / 'resources.qrc', encoding='utf-8', xml_declaration=True)


def check(require_complete=False):
    strings = set(sources())
    metadata = json.loads((DATA / 'catalogs.json').read_text(encoding='utf-8'))
    seeds = json.loads((DATA / 'seed-translations.json').read_text(encoding='utf-8'))
    tags = [item['tag'] for item in metadata]
    if len(tags) != len(set(tags)) or set(tags) != {'en', *seeds['languages']}:
        raise ValueError('Catalog locale inventory mismatch')
    resource = ET.parse(DATA / 'resources.qrc')
    groups = resource.findall('./qresource')
    if len(groups) != 1 or groups[0].get('prefix') != '/i18n':
        raise ValueError('Embedded catalog resource prefix mismatch')
    resource_names = {f.get('alias'): f.text for f in resource.findall('./qresource/file')}
    expected = {'catalogs.json': 'catalogs.json', **{f'soundcurrent_{tag}.qm': f'soundcurrent_{tag}.qm' for tag in tags}}
    if resource_names != expected or len(resource.findall('./qresource/file')) != len(expected):
        raise ValueError('Embedded catalog resources differ from metadata')
    missing = 0
    for item in metadata:
        ts = DATA / f"soundcurrent_{item['tag']}.ts"
        qm = ts.with_suffix('.qm')
        if digest(ts) != item['tsSha256'] or digest(qm) != item['qmSha256']:
            raise ValueError(f'Stale QM/TS: {ts}')
        tree = ET.parse(ts)
        if tree.getroot().get('language') != item['tag'].replace('-', '_') or [c.findtext('name') for c in tree.findall('./context')] != ['SoundCurrent']:
            raise ValueError(f'Wrong locale or context: {ts}')
        messages = entries(ts)
        if set(messages) != strings:
            raise ValueError(f'Run --update after UI changes: {ts}')
        done = 0
        for source, message in messages.items():
            if finished(message):
                try:
                    validate_text(source, message.findtext('translation'))
                except ValueError as error:
                    raise ValueError(f'{ts}: {source}: {error}') from error
                done += 1
            else:
                missing += 1
        if item['tag'] in seeds.get('requiredCompleteLocales', []) and done != len(strings):
            raise ValueError(f"Previously populated locale has new untranslated messages: {item['tag']}")
        if done != item['translated'] or len(strings) != item['total']:
            raise ValueError(f'Coverage metadata mismatch: {ts}')
    if require_complete and missing:
        raise ValueError(f'{missing} unfinished translations remain')
    print(f'PASS: {len(metadata)} catalogs, {len(strings)} source messages; {missing} unfinished entries; structural checks and compiled hashes (linguistic accuracy unverified)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--update', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    if args.update:
        update()
    check(args.require_complete)
