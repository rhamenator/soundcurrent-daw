#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Retain original bounded geometry/processor observations, without binaries."""
from pathlib import Path
import argparse,hashlib,json,subprocess,zipfile
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('cache',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
cache=args.cache.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert not (out/'capture.zip').exists() and not (out/'manifest.json').exists(), 'Never replace an existing capture'
entries={};sha=lambda data:hashlib.sha256(data).hexdigest()
def add(name,path):
    assert name not in entries;entries[name]=path.read_bytes()
for name in ['requests.json','contracts.json','stdout.jsonl','stderr.log','summary.json']:add('geometry-initial/'+name,cache/'oracle-1'/name)
for path in sorted((cache/'executed-sources-1').iterdir()):add('executed-initial/'+path.name,path)
for path in sorted((root/'experiments/warp-map').iterdir()):
    if path.is_file():add('experiment/'+path.name,path)
for name in ['processes.json','summary.json','analysis.json','analysis-initial.json']:add('candidates/'+name,cache/'run-1'/name)
records=json.loads(entries['candidates/processes.json'])
for r in records:
    folder=cache/'run-1'/f"case-{r['id']:02d}"
    add(f"candidates/case-{r['id']:02d}/render.json",folder/'render.json')
    for name,key in [('source.wav','sourceSha256'),('rendered.wav','renderSha256')]:
        data=(folder/name).read_bytes();digest=sha(data);assert digest==r[key]
        # Exact-byte sharing only. Every case's raw/render hash still resolves
        # to a complete WAVE; no PCM is omitted or regenerated during retention.
        label='waves/'+digest+'.wav'
        if label in entries:assert entries[label]==data
        else:entries[label]=data
for name in ['failure.json','retain.py']:add('retention-failed/'+name,cache/'retention-failed'/name)
add('retention-failed/inspection.log',cache/'retained-inspection-1.log')
for name in ['run-1.log','analysis-1.log','analysis-2.log','cmake-configure.log','cmake-build.log','cmake-build-final.log','cmake-test-1.log','cmake-test-final.log']:add('logs/'+name,cache/name)
line=next(v.split(': ',1)[1] for v in entries['logs/cmake-test-final.log'].decode().splitlines() if v.startswith('1: {'))
final=json.loads(line);folder=Path(final['evidenceDirectory'])
for name in ['requests.json','contracts.json','stdout.jsonl','stderr.log','summary.json']:add('geometry-final/'+name,folder/name)
inputs={str(f.relative_to(root)):sha(f.read_bytes()) for parent in ['include','src','third_party'] for f in sorted((root/parent).rglob('*')) if f.is_file()}
metadata={'format':'sc-warp-map-build-inputs-v1','productionBase':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
          'sourceWasUncommitted':True,'compiler':subprocess.check_output(['c++','--version'],text=True).splitlines()[0],
          'inputs':inputs,'executables':{},'libraries':{},'sourceSnapshots':{},'nativeWindowsBuilt':False,'nativeAudio':False}
metadata['manualBuildCommands']=[
 'ionice -c3 nice -n19 c++ -std=c++20 -O2 -DNDEBUG -fno-fast-math -Wall -Wextra -Wpedantic -Iinclude -Ithird_party experiments/warp-map/geometry_probe.cpp experiments/warp-map/warp_map.cpp .cache/build-stretch-region/libsc-session.a -pthread -o .cache/region-quality/sc-warp-geometry-probe',
 'ionice -c3 nice -n19 c++ -std=c++20 -O2 -DNDEBUG -fno-fast-math -Wall -Wextra -Wpedantic -Iinclude -Ithird_party -Ithird_party/rubberband experiments/warp-map/render_probe.cpp experiments/warp-map/warp_map.cpp -Wl,--start-group .cache/build-stretch-region/libsc-rubberband.a .cache/build-stretch-region/libsc-session.a -Wl,--end-group -pthread -o .cache/region-quality/sc-warp-render-probe']
for name,path in [('initialGeometry',root/'.cache/region-quality/sc-warp-geometry-probe'),('candidateRenderer',root/'.cache/region-quality/sc-warp-render-probe'),('finalGeometry',root/'.cache/build-warp-map-geometry/sc-warp-map-probe')]:metadata['executables'][name]={'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
for path in [root/'.cache/build-stretch-region/libsc-session.a',root/'.cache/build-stretch-region/libsc-rubberband.a',root/'.cache/build-warp-map-geometry/libsc-session.a']:metadata['libraries'][str(path.relative_to(root))]={'bytes':path.stat().st_size,'sha256':sha(path.read_bytes())}
for name in ['CMakeLists.txt','.github/workflows/ci.yml']:add('build-inputs/'+name,root/name);metadata['sourceSnapshots'][name]=sha((root/name).read_bytes())
entries['build-inputs.json']=(json.dumps(metadata,indent=2)+'\n').encode()
assert sum(map(len,entries.values()))<48*1024*1024
with zipfile.ZipFile(out/'capture.zip','x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for name,data in sorted(entries.items()):
        info=zipfile.ZipInfo(name,date_time=(2026,10,10,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16;z.writestr(info,data)
data=(out/'capture.zip').read_bytes();assert len(data)<16*1024*1024
manifest={'format':'sc-warp-map-local-capture-v1','sha256':sha(data),'bytes':len(data),'entries':{n:{'bytes':len(d),'sha256':sha(d)} for n,d in sorted(entries.items())},
          'actualCandidateProcesses':52,'allSourceAndRenderedWavesRetained':True,'initialAnalysisRetained':True,'shippingProcessorChanged':False,'nativeWindowsReplayed':False,'nativeAudio':False,'fullQualityQualified':False,'productBinaryUploaded':False}
manifest['wavesContentAddressed']=True
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'entries':len(entries),'bytes':len(data),'uncompressedBytes':sum(map(len,entries.values())),'finalGeometryChecks':final['checks']}))
