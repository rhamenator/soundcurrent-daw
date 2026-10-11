from pathlib import Path
import subprocess,json,hashlib,gzip,re
r=Path('.cache/manual-native-failure');out=Path('tests/results/M2/2026-10-11-native-take-stage-diagnostics');q=json.loads((r/'qualified-profiles.json').read_text());f=json.loads((r/'frozen-inputs.json').read_text());assert q['source']==f['source']=='6b6d6838de7dd04196d62a3c3a6ced1fd97ba25b';assert q['tree']==f['tree'];assert f['changedSourceFiles']==['tests/manual_recording_tests.cpp']
out.mkdir(parents=True,exist_ok=True);copies=['qualify.py','retain.py','frozen-inputs.json','qualified-profiles.json','warning-provenance.json','source-delta.diff','qualification-driver.log','windows-fixture-compile.log'];records={};observations={}
for label in ['release','sanitizer','tsan']:
 b=r/('build-'+label);test=(r/(label+'-tests.log')).read_text();log=(r/(label+'-LastTest.log')).read_text();assert '100% tests passed, 0 tests failed out of 6' in test
 assert 'stage=submit-out' in log and 'capture_status=2' in log and 'retired=8192' in log and 'concurrent_callback_fault present=1 detected=12' in log and 'failed_capture=0' in log and 'concurrent_retained_error present=0' in log and 'message=active audio write failure' in log
 metrics=[json.loads(l) for l in log.splitlines() if l.startswith('{"checks":')];manual=next(m for m in metrics if 'maximum_output_difference' in m and 'rt_violations' in m);assert manual['rt_violations']==0 and manual['maximum_output_difference']==0
 assert 'guardedAllocateFreeLock=0' in log
 observations[label]={'sixSuitesPassed':True,'seconds':float(re.search(r'Total Test time \(real\) =\s*([0-9.]+)',test).group(1)),'manualOwnerChecks':manual['checks'],'maximumOutputDifference':manual['maximum_output_difference'],'outputPeak':manual['output_peak'],'rtViolations':manual['rt_violations']}
 for n,sha in q['executableHashes'][label].items():assert hashlib.sha256((b/n).read_bytes()).hexdigest()==sha
 for suffix in ['configure','build','tests','LastTest']:copies.append(label+'-'+suffix+'.log')
 copies.append(label+'-CMakeCache.txt');(r/(label+'-CMakeCache.txt')).write_bytes((b/'CMakeCache.txt').read_bytes())
 for n in ['CMakeFiles/sc-manual-recording-tests.dir/tests/manual_recording_tests.cpp.o']:
  records[label+'/'+n]=hashlib.sha256((b/n).read_bytes()).hexdigest()
for x in f['selectedInputs']:
 raw=Path(x['path']).read_bytes();assert len(raw)==x['checkoutBytes'] and hashlib.sha256(raw).hexdigest()==x['checkoutSha256']
obj=r/'windows-fixture.obj';assert obj.stat().st_size>1000;assert not (r/'windows-fixture-compile.log').read_text().strip();records['windows-fixture.obj']=hashlib.sha256(obj.read_bytes()).hexdigest()
summary={'source':q['source'],'tree':q['tree'],'preceding':f['preceding'],'changedSourceFiles':f['changedSourceFiles'],'selectedInputs':len(f['selectedInputs']),'observations':observations,'linuxExecutableHashes':q['executableHashes'],'objectHashes':records,'nativeWindowsRuntimeExecuted':False,'nativeHardwareQualified':False,'originalWindowsRootCauseEstablished':False,'productionSourcesChanged':False,'originalFailurePacket':'../2026-10-11-native-punch-out-failure/summary.json','knownExistingWarnings':json.loads((r/'warning-provenance.json').read_text())}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');members={}
for name in copies:
 raw=(r/name).read_bytes()
 if name.endswith('.log') or name.endswith('-CMakeCache.txt'):
  dest=name+'.gz';packed=gzip.compress(raw,mtime=0);assert gzip.decompress(packed)==raw
 else:dest=name;packed=raw
 (out/dest).write_bytes(packed);members[dest]={'bytes':len(packed),'sha256':hashlib.sha256(packed).hexdigest(),'originalBytes':len(raw),'originalSha256':hashlib.sha256(raw).hexdigest()}
raw=(out/'summary.json').read_bytes();members['summary.json']={'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
(out/'manifest.json').write_text(json.dumps({'members':members,'retainedAssets':'No executables, objects or media; original logs losslessly compressed','sourceQualificationScope':'Six selected suites; test diagnostic change only'},indent=2)+'\n');print(json.dumps({'source':summary['source'],'observations':observations,'members':len(list(out.iterdir())),'bytes':sum(p.stat().st_size for p in out.iterdir())}))
