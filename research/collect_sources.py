#!/usr/bin/env python3
"""Freeze primary source URLs and hashes; raw copyrighted pages stay ignored."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from urllib.parse import urljoin
import hashlib, json, requests
from bs4 import BeautifulSoup
from reference_labels import neutral_title

ROOT = Path(__file__).resolve().parents[1]
if (ROOT / 'research' / 'sources.json').exists():
    raise SystemExit('Source manifest already frozen. Refresh in a separate baseline workspace, then review the ADR and source delta.')
CACHE = ROOT / '.cache' / 'sources'
CACHE.mkdir(parents=True, exist_ok=True)
session = requests.Session()

def fetch(url):
    r = session.get(url, timeout=40)
    r.raise_for_status()
    return r.content

cubase = 'https://www.steinberg.help/r/cubase-pro/15.0/en'
bitwig = 'https://www.bitwig.com/userguide/latest/'
data = fetch(cubase)
soup = BeautifulSoup(data, 'html.parser')
links = [(a.get_text(' ', strip=True), urljoin(cubase, a.get('href', ''))) for a in soup.find_all('a', href=True)]
(CACHE/'cubase-index.html').write_bytes(data)
(CACHE/'cubase-links.json').write_text(json.dumps(links, indent=2))
topics = [
 'Recording', 'Monitoring', 'Punch', 'Cycle', 'Lanes', 'Comp', 'Group Editing',
 'Crossfades', 'Fades', 'AudioWarp', 'VariAudio', 'Time Stretch', 'MixConsole',
 'VCA', 'Side-Chain', 'External Instruments', 'External Effects', 'Delay Compensation',
 'Automation Modes', 'Tempo Track', 'Time Signature', 'Note Expression',
 'Expression Maps', 'MIDI Remote', 'Control Room', 'Cue Mix', 'Talkback',
 'Loudness Measurement', 'Stem Separation', 'Ambisonics Mixes', 'Surround Sound',
 'Dolby Atmos', 'Video', 'Timecode', 'Export Audio Mixdown', 'AAF', 'OMF',
 'MusicXML', 'Missing', 'Auto Save', 'Backups', 'Edit History',
 'Sampler Tracks', 'MediaBay', 'Modulators', 'Pattern Editor', 'Chord Track',
 'Logical Editor', 'Score', 'Freeze', 'Direct Offline Processing', 'Audio Alignment',
 'Retrospective', 'Quantiz', 'Pool', 'Project Logical', 'VST Plug-in Manager'
]
selected = {}
for term in topics:
    candidates = [(t,u) for t,u in links if term.lower() in t.lower()]
    # The first two topics give an overview plus a more specific workflow.
    for title,url in candidates[:2]:
        if '/topics/' in url: selected[url] = title

bitdata = fetch(bitwig)
bsoup = BeautifulSoup(bitdata, 'html.parser')
for a in bsoup.find_all('a',href=True):
    url = urljoin(bitwig,a['href'])
    if '/userguide/latest/' in url and url.rstrip('/') != bitwig.rstrip('/'):
        selected[url] = a.get_text(' ',strip=True)

selected.update({
 'https://www.bitwig.com/download/': 'Bitwig release downloads',
 'https://www.bitwig.com/dl/Bitwig%20Studio/6.1.3/release_notes/': 'Bitwig 6.1.3 release notes',
 'https://downloads.bitwig.com/6.1/Release-Notes-6.1.pdf': 'Bitwig 6.1 and 6.0 documentation',
 'https://o.steinberg.net/en/support/downloads/cubase_15.html': 'Cubase 15 release downloads',
 cubase: 'Cubase Pro 15 manual index',
 bitwig: 'Bitwig general manual 5.3 index',
 'https://www.steinberg.help/api/khub/documents/O4PvzgK5U8lOyn4ANi_gKg/content': 'Cubase Pro 15 manual PDF',
 'https://www.steinberg.net/cubase/compare-editions/': 'Cubase edition boundaries',
})

def collect(item):
    url,title = item
    key = hashlib.sha256(url.encode()).hexdigest()[:16]
    result = dict(id=key,title=neutral_title(title),url=url,accessed='2026-10-05')
    try:
        raw = fetch(url)
        result.update(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),retrieved=True)
        ext = 'pdf' if raw.startswith(b'%PDF') else 'html'
        (CACHE/f'{key}.{ext}').write_bytes(raw)
        if ext == 'html':
            text = BeautifulSoup(raw,'html.parser').get_text(' ',strip=True)
            (CACHE/f'{key}.txt').write_text(text)
    except Exception as e:
        result.update(retrieved=False,error=str(e))
    return result

with ThreadPoolExecutor(max_workers=8) as pool:
    records = list(pool.map(collect,selected.items()))
(ROOT/'research/sources.json').write_text(json.dumps(records,indent=2)+'\n')
print(json.dumps({'sources':len(records),'failed':[r['url'] for r in records if not r['retrieved']]},indent=2))
