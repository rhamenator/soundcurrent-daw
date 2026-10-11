from pathlib import Path
import subprocess,json,hashlib,gzip,re
r=Path('.cache/native-ready-consumer');out=Path('tests/results/M2/2026-10-11-native-ready-consumer');q=json.loads((r/'qualified-profiles.json').read_text());f=json.loads((r/'frozen-inputs.json').read_text());w=json.loads((r/'windows-qualification.json').read_text());assert q['source']==f['source']==w['source']=='e5c7a468b790b7440bfb998ef48d7d3e6bd21d31';assert q['tree']==f['tree']==w['tree'];assert len(q['executableHashes'])==3
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==q['source'];assert not subprocess.check_output(['git','status','--porcelain'],text=True)
for x in f['selectedInputs']:
 raw=Path(x['path']).read_bytes();assert len(raw)==x['checkoutBytes'] and hashlib.sha256(raw).hexdigest()==x['checkoutSha256']
files=['qualify.py','qualify-windows.py','freeze-inputs.py','retain.py','frozen-inputs.json','qualified-profiles.json','qualification-commands.json','windows-qualification.json','warning-provenance.json','source-delta.diff','windows-configure.log','windows-build.log'];observations={};obj={}
for label in ['release','sanitizer','tsan']:
 b=r/('build-'+label);test=(r/(label+'-tests.log')).read_text();log=(r/(label+'-LastTest.log')).read_text();assert '100% tests passed, 0 tests failed out of 9' in test
 rows=[l for l in log.splitlines() if l.startswith('composed startup channels=')];assert len(rows)==7 and all('maxError=0' in l and 'fakeSDK=true' in l for l in rows);assert sum('failedStop=1' in l for l in rows)==1
 assert 'concurrent native startup captureOwners=1 processingOwners=1 renderOwners=1 fullFIFO=4097 maxError=0 guarded=0 fakeSDK=true' in log
 checks=int(re.search(r'(\d+) composed native monitor startup checks passed; guarded allocate/free/lock=0',log).group(1))
 observations[label]={'nineSuitesPassed':True,'seconds':float(re.search(r'Total Test time \(real\) =\s*([0-9.]+)',test).group(1)),'composedChecks':checks,'composedRows':rows,'concurrentOwners':3,'maximumPcmError':0,'guardedAllocateFreeLock':0,'simulatedSdkOnly':True}
 for n,sha in q['executableHashes'][label].items():assert hashlib.sha256((b/n).read_bytes()).hexdigest()==sha
 for suffix in ['configure','build','tests','LastTest']:files.append(label+'-'+suffix+'.log')
 cache=label+'-CMakeCache.txt';(r/cache).write_bytes((b/'CMakeCache.txt').read_bytes());files.append(cache)
 for n in ['CMakeFiles/sc-native-monitor-startup-tests.dir/tests/native_monitor_startup_tests.cpp.o','CMakeFiles/sc-monitor-processing.dir/src/wasapi_monitor_render.cpp.o']:
  obj[label+'/'+n]=hashlib.sha256((b/n).read_bytes()).hexdigest()
for tag,x in w['objects'].items():
 assert hashlib.sha256((r/('windows-'+tag+'.obj')).read_bytes()).hexdigest()==x['sha256'];assert not (r/('windows-'+tag+'-compile.log')).read_text().strip();files.append('windows-'+tag+'-compile.log')
cache='windows-CMakeCache.txt';(r/cache).write_bytes((r/'build-windows/CMakeCache.txt').read_bytes());files.append(cache)
for n in ['qualify.py','qualify-windows.py','freeze-inputs.py','retain.py','qualification-commands.json']:assert (r/n).exists()
# The preceding full-tree merge conflict did not alter the original committed candidate.
for n in ['original-stages.txt','original-merge.diff','original-conflicted-file.cpp','stage-1.cpp','stage-2.cpp','stage-3.cpp','resolution.json']:files.append('main-inheritance/'+n)
summary={'format':'sc-native-ready-consumer-v1','source':q['source'],'tree':q['tree'],'precedingProtectedMain':f['preceding'],'changedSourceFiles':f['changedSourceFiles'],'selectedInputs':len(f['selectedInputs']),'observations':observations,'linuxExecutableHashes':q['executableHashes'],'linuxObjectHashes':obj,'windows':w,'originalWindowsRootsResolved':False,'nativeHardwareQualified':False,'nativeWindowsRuntimeExecuted':False,'factoryEnabled':False,'state11MigrationImplemented':False,'executablesIncluded':False,'mediaIncluded':False,'fullGoalComplete':False}
out.mkdir(parents=True,exist_ok=True);(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');members={}
for name in files:
 raw=(r/name).read_bytes();dest=name.replace('/','--');packed=raw
 if name.endswith(('.log','-CMakeCache.txt','.diff','.cpp')):dest+='.gz';packed=gzip.compress(raw,mtime=0);assert gzip.decompress(packed)==raw
 (out/dest).write_bytes(packed);members[dest]={'bytes':len(packed),'sha256':hashlib.sha256(packed).hexdigest(),'originalBytes':len(raw),'originalSha256':hashlib.sha256(raw).hexdigest()}
raw=(out/'summary.json').read_bytes();members['summary.json']={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
(out/'manifest.json').write_text(json.dumps({'members':members,'scope':'Nine selected suites per Release, ASan/UBSan, TSan; MinGW SDK/four tests and five compile objects; no native Windows execution or hardware qualification','retainedAssets':'Source bindings, commands and full logs; logs losslessly compressed, no executables/objects/media'},indent=2)+'\n')
print(json.dumps({'source':q['source'],'observations':{k:{a:b for a,b in v.items() if a!='composedRows'}for k,v in observations.items()},'members':len(list(out.iterdir())),'bytes':sum(p.stat().st_size for p in out.iterdir())}),flush=True)
