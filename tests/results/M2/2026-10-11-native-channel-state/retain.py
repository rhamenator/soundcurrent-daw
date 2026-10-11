from pathlib import Path
import subprocess,json,hashlib,gzip,re
r=Path('.cache/native-channel-state');out=Path('tests/results/M2/2026-10-11-native-channel-state');q=json.loads((r/'qualified-profiles.json').read_text());f=json.loads((r/'frozen-inputs.json').read_text());w=json.loads((r/'windows-qualification.json').read_text());head='9d5da1c5889dc57af20fe70408a43932fcee6f61'
assert q['source']==f['source']==w['source']==head and q['tree']==f['tree']==w['tree'] and len(q['executableHashes'])==3
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head and not subprocess.check_output(['git','status','--porcelain'],text=True)
for x in f['selectedInputs']:
 raw=Path(x['path']).read_bytes();assert len(raw)==x['checkoutBytes'] and hashlib.sha256(raw).hexdigest()==x['checkoutSha256']
files=['qualify.py','qualify-windows.py','freeze-inputs.py','retain.py','frozen-inputs.json','qualified-profiles.json','qualification-commands.json','windows-qualification.json','warning-source-provenance.json','source-delta.diff','windows-configure.log','windows-build.log','development-first-failure.log','development-metadata-read-failure.log'];observations={};objects={}
for label in ['release','sanitizer','tsan']:
 b=r/('build-dev' if label=='release' else 'build-'+label);test=(r/(label+'-tests.log')).read_text();log=(r/(label+'-LastTest.log')).read_text();assert '100% tests passed, 0 tests failed out of 4' in test
 rows=[l for l in log.splitlines() if l.startswith('composed startup inputChannels=')];mapped=[l for l in rows if 'inputChannels=1 outputChannels=2' in l]
 assert len(rows)==12 and len(mapped)==4 and all('maxError=0' in l and 'fakeSDK=true' in l for l in rows)
 assert sum('failedStop=1' in l for l in mapped)==1 and {re.search(r'sourceRate=(\d+)',l).group(1) for l in mapped}=={'44100','48000','96000'}
 states=[l for l in log.splitlines() if l.startswith('composed state schemaMinor=')];assert len(states)==12 and sum('schemaMinor=1 ' in l for l in states)==1 and all('retainedRawHash=true fullReadyRoundtrip=true' in l for l in states)
 assert sum(l.startswith('diagnostic state1.2 ') for l in log.splitlines())==14
 assert 'concurrent native startup captureOwners=1 processingOwners=1 renderOwners=1 fullFIFO=4097 maxError=0 guarded=0 fakeSDK=true' in log
 for x in ['4 actual process interruption boundaries','2 actual migration process boundaries','2 actual matrix publication process boundaries']:assert x in log
 observations[label]={'fourSuitesPassed':True,'seconds':float(re.search(r'Total Test time \(real\) =\s*([0-9.]+)',test).group(1)),'nativeOutcomeChecks':int(re.search(r'(\d+) durable native outcome checks passed',log).group(1)),'composedChecks':int(re.search(r'(\d+) composed native monitor startup checks passed; guarded allocate/free/lock=0',log).group(1)),'processingChecks':int(re.search(r'(\d+) bounded monitor processing checks passed; guarded allocate/free/lock=0',log).group(1)),'composedPersistenceRuns':12,'mappedComposedPersistenceRuns':4,'legacy11ComposedMigrationRuns':1,'diagnosticPersistenceRuns':14,'nativeProcessBoundaries':4,'migrationProcessBoundaries':2,'matrixPublicationProcessBoundaries':2,'composedRows':rows,'maximumPcmError':0,'guardedAllocateFreeLock':0,'simulatedSdkOnly':True}
 for n,sha in q['executableHashes'][label].items():assert hashlib.sha256((b/n).read_bytes()).hexdigest()==sha
 for suffix in ['configure','build','tests','LastTest']:files.append(label+'-'+suffix+'.log')
 cache=label+'-CMakeCache.txt';(r/cache).write_bytes((b/'CMakeCache.txt').read_bytes());files.append(cache)
 for n in ['CMakeFiles/sc-monitor-processing.dir/src/monitor_channel_map.cpp.o','CMakeFiles/sc-monitor-processing.dir/src/monitor_processing.cpp.o','CMakeFiles/sc-monitor-processing.dir/src/monitor_startup_handoff.cpp.o','CMakeFiles/sc-native-monitor-startup-tests.dir/tests/native_monitor_startup_tests.cpp.o','CMakeFiles/sc-native-original-recording.dir/src/native_original_recording.cpp.o','CMakeFiles/sc-native-original-recording-tests.dir/tests/native_original_recording_tests.cpp.o']:
  objects[label+'/'+n]={'bytes':(b/n).stat().st_size,'sha256':hashlib.sha256((b/n).read_bytes()).hexdigest()}
for tag,x in w['objects'].items():
 assert hashlib.sha256((r/('windows-'+tag+'.obj')).read_bytes()).hexdigest()==x['sha256'] and not (r/('windows-'+tag+'-compile.log')).read_text().strip();files.append('windows-'+tag+'-compile.log')
assert len(w['objects'])==7
cache='windows-CMakeCache.txt';(r/cache).write_bytes((r/'build-windows/CMakeCache.txt').read_bytes());files.append(cache)
windowsBins={}
for n in ['sc-monitor-processing-tests.exe','sc-monitor-startup-tests.exe','sc-native-monitor-startup-tests.exe','libsc-wasapi-monitor-render.a']:
 p=r/'build-windows'/n;windowsBins[n]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
summary={'format':'sc-native-channel-state-v1','source':head,'tree':q['tree'],'precedingSource':f['preceding'],'selectedInputs':len(f['selectedInputs']),'observations':observations,'linuxExecutableHashes':q['executableHashes'],'linuxObjectHashes':objects,'windows':w,'windowsBinaryHashes':windowsBins,'state12MatrixAndMigrationImplemented':True,'originalWindowsRootsResolved':False,'nativeHardwareQualified':False,'nativeWindowsRuntimeExecuted':False,'factoryEnabled':False,'executablesIncluded':False,'mediaIncluded':False,'fullGoalComplete':False}
out.mkdir(parents=True,exist_ok=False);(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');members={}
for name in files:
 raw=(r/name).read_bytes();dest=name;packed=raw
 if name.endswith(('.log','-CMakeCache.txt','.diff')):dest+='.gz';packed=gzip.compress(raw,mtime=0);assert gzip.decompress(packed)==raw
 (out/dest).write_bytes(packed);members[dest]={'bytes':len(packed),'sha256':hashlib.sha256(packed).hexdigest(),'originalBytes':len(raw),'originalSha256':hashlib.sha256(raw).hexdigest(),'lossless':dest.endswith('.gz')}
raw=(out/'summary.json').read_bytes();members['summary.json']={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
(out/'manifest.json').write_text(json.dumps({'members':members,'scope':'Four selected suites per Release, ASan/UBSan, TSan; MinGW SDK/three fixtures and seven warning-free objects, including persistence-enabled fixtures; no native Windows execution or hardware qualification','retainedAssets':'Source bindings, commands, configurations, hashes and full logs; no executable/object/media files'},indent=2)+'\n')
print(json.dumps({'source':head,'profiles':{k:{a:b for a,b in v.items() if a!='composedRows'}for k,v in observations.items()},'members':len(list(out.iterdir())),'bytes':sum(p.stat().st_size for p in out.iterdir())}),flush=True)
