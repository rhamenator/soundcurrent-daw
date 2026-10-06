#!/usr/bin/env python3
"""Pinned, bounded collector for GPL Spinorama generated EQ; never executes upstream Python.
Metadata literals are read with AST; nonliteral fields are ignored, never evaluated.
Run manually; not an application background scraper. See docs/equipment-profiles.md.
"""
import ast, concurrent.futures, hashlib, json, math, re, time, urllib.parse, urllib.request
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
COMMIT = 'acc757bb98d63327092ee537bde25d9c227811f3'
BASE = f'https://raw.githubusercontent.com/pierreaubert/spinorama/{COMMIT}/'
CACHE = ROOT / '.cache/equipment-sources'
CACHE.mkdir(parents=True, exist_ok=True)
def fetch(path):
    dest = CACHE / hashlib.sha256(path.encode()).hexdigest()
    if dest.exists(): return dest.read_bytes()
    for attempt in range(3):
        try:
            req=urllib.request.Request(BASE+urllib.parse.quote(path),headers={'User-Agent':'SoundCurrent-profile-research/1.0'})
            with urllib.request.urlopen(req,timeout=30) as f:
                b=f.read(2*1024*1024+1)
            if len(b)>2*1024*1024: raise ValueError('source over 2 MiB')
            dest.write_bytes(b); return b
        except Exception:
            if attempt==2: raise
            time.sleep(1+attempt)
# Tree snapshot is explicitly pinned, bounded to 16 MiB.
with urllib.request.urlopen(f'https://api.github.com/repos/pierreaubert/spinorama/git/trees/{COMMIT}?recursive=1',timeout=30) as f:
    tree=json.loads(f.read(16*1024*1024))
if tree.get('truncated'): raise RuntimeError('Upstream tree truncated')
metadata={}
for item in tree['tree']:
    path=item['path']
    if re.fullmatch(r'datas/speaker_[a-z0-9]+\.py',path):
        module=ast.parse(fetch(path).decode())
        for node in module.body:
            value=node.value if isinstance(node,(ast.Assign,ast.AnnAssign)) else None
            if isinstance(value,ast.Dict):
                for key,val in zip(value.keys,value.values):
                    try:
                        name=ast.literal_eval(key)
                        # Keep direct literal metadata fields only (nested unsupported expressions ignored).
                        fields={}
                        if isinstance(val,ast.Dict):
                            for k,v in zip(val.keys,val.values):
                                try: fields[ast.literal_eval(k)]=ast.literal_eval(v)
                                except (ValueError,TypeError): pass
                        metadata[name]=fields
                    except (ValueError,TypeError): pass
pattern=re.compile(r'Filter\s+\d+:\s+ON\s+(PK|LS|HS)\s+Fc\s+([\d.]+)\s+Hz\s+Gain\s+([+\-\d.]+)\s+dB\s+Q\s+([\d.]+)')
eq_files={item['path'] for item in tree['tree'] if item['path'].startswith('datas/eq/') and item['type']=='blob'}
model_files={}
for path in eq_files:
    parts=path.split('/')
    if len(parts)==4:model_files.setdefault(parts[2],set()).add(parts[3])
# Prefer the upstream default. Alternate published files are explicitly attributed.
priority=['iir-autoeq.txt','iir-autoeq-score.txt','iir-autoeq-lw.txt','iir.txt','iir-flipflop.txt']
paths=[]
for model,files in sorted(model_files.items()):
    for filename in priority:
        if filename in files:paths.append(f'datas/eq/{model}/{filename}');break
unavailable=[dict(model=model,reason='No published IIR correction file') for model,files in model_files.items() if not any(f in files for f in priority)]
def collect(path):
    try:
        original=path;seen=set()
        for _ in range(4):
            if path in seen:raise ValueError('Cyclic upstream profile pointer')
            seen.add(path);b=fetch(path);text=b.decode().strip()
            if re.fullmatch(r'iir[\w.-]*\.txt',text):
                candidate=path.rsplit('/',1)[0]+'/'+text
                if candidate not in eq_files:raise ValueError('Missing upstream profile pointer target')
                path=candidate
            else:break
        else:raise ValueError('Too many upstream pointer hops')
        name=path.split('/')[2];meta=metadata.get(name,{})
        filters=[]
        for t,hz,gain,q in pattern.findall(text):
            hz,gain,q=map(float,(hz,gain,q))
            if not all(map(math.isfinite,(hz,gain,q))) or not 20<=hz<=20000:continue
            if hz<80 and gain>0:continue
            filters.append(dict(type=t,frequency=hz,gain=max(-6,min(6,gain)),q=max(.1,min(6,q))))
        if not 1<=len(filters)<=16:return None,dict(model=name,reason='No admissible 1–16 filter correction')
        brand=meta.get('brand',name.split()[0]);model=meta.get('model',name.removeprefix(brand).strip()) or name
        # No invented product lineage: unclassified family is explicit and user editable.
        family='Unclassified series'
        for known_brand,prefix,series in [('JBL','30','3 Series MkII'),('Yamaha','HS','HS Series'),('Kali','LP-','Lone Pine')]:
            if brand==known_brand and model.startswith(prefix): family=series
        measure=re.search(r'^EQ for .*? computed from (.*?) data',text,re.M)
        origin=measure.group(1) if measure else 'See upstream published EQ'
        shapes={'bookshelves':'Bookshelf','floorstanders':'Floorstanding','center':'Center','surround':'Surround','inwall':'In-wall','liveportable':'Portable PA','toursound':'Touring PA','cinema':'Cinema','outdoor':'Outdoor','omnidirectional':'Omnidirectional','columns':'Column','panel':'Panel','cbt':'Constant beamwidth','soundbar':'Soundbar'}
        equipment_type=shapes.get(meta.get('shape'),'Unclassified')
        return dict(equipmentType=equipment_type,powerType=meta.get('type','Unknown'),schema=2,id='spinorama-'+hashlib.sha256(name.encode()).hexdigest()[:24],kind='speaker',brand=brand,family=family,model=model,measurementSource=BASE+urllib.parse.quote(path),conditions=f'Published model-level correction from {origin}; source file {path.rsplit("/",1)[-1]}; not room calibration. Gains limited to ±6 dB, Q 0.1–6; positive filters below 80 Hz omitted. Verify model/version and measurement at source.',provenance=f'Spinorama GPL-3.0; commit {COMMIT}; selected file {original}; resolved file {path}; source SHA256 {hashlib.sha256(b).hexdigest()}; family classification is explicit; see docs/equipment-profiles.md.',custom=False,filters=filters,response=[]),None
    except Exception as e:return None,dict(model=path,error=str(e))
profiles=[];gaps=list(unavailable)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    for i,(p,gap) in enumerate(pool.map(collect,paths)):
        if p:profiles.append(p)
        if gap:gaps.append(gap)
        if i%100==0:print(f'{i}/{len(paths)}',flush=True)
profiles.sort(key=lambda p:(p['brand'].casefold(),p['family'],p['model'].casefold()))
(ROOT/'data/equipment/spinorama.json').write_text(json.dumps(profiles,ensure_ascii=False,indent=2)+'\n')
(ROOT/'data/equipment/collection-report.json').write_text(json.dumps(dict(commit=COMMIT,available=len(paths),accepted=len(profiles),brands=len({p['brand'] for p in profiles}),gaps=gaps),indent=2)+'\n')
print(f'Collected {len(profiles)} profiles / {len({p["brand"] for p in profiles})} brands; {len(gaps)} gaps',flush=True)
