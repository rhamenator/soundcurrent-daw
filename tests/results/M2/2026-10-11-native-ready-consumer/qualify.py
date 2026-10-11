from pathlib import Path
import subprocess,json,hashlib,shutil,time
r=Path('.cache/native-ready-consumer');source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert source=='e5c7a46bc4a95dc94771f1436d8a7947f36d99af' if False else source.startswith('e5c7a46')
assert not subprocess.check_output(['git','status','--porcelain'],text=True)
pre=['nice','-n','15','ionice','-c','2','-n','7']
targets=['sc-native-monitor-startup-tests','sc-monitor-startup-tests','sc-wasapi-monitor-render-tests','sc-wasapi-render-lifecycle-tests','sc-native-original-recording-tests','sc-original-recording-tests','sc-monitor-recording-tests','sc-monitor-clock-handoff-tests','sc-monitor-processing-tests']
selector='^(native-monitor-startup|bounded-monitor-startup|wasapi-monitor-render-binding|wasapi-render-end|original-native-outcomes|original-raw-recording|combined-monitor-recording|original-monitor-clock-handoffs|bounded-monitor-processing)$'
records={};commands=[]
for label,ctype,flags,link in [('release','Release','',''),('sanitizer','Debug','-fsanitize=address,undefined -fno-omit-frame-pointer','-fsanitize=address,undefined'),('tsan','Debug','-fsanitize=thread -fno-omit-frame-pointer -O1','-fsanitize=thread')]:
 b=r/('build-'+label)
 steps=[('configure',['cmake','-S','.','-B',str(b),'-G','Ninja','-DCMAKE_BUILD_TYPE='+ctype,'-DSC_BUILD_DESKTOP=OFF','-DSC_BUILD_MEDIA=ON','-DCMAKE_CXX_FLAGS='+flags,'-DCMAKE_EXE_LINKER_FLAGS='+link]),('build',['cmake','--build',str(b),'--parallel','1','--target',*targets]),('tests',['ctest','--test-dir',str(b),'-R',selector,'--output-on-failure','--no-tests=error'])]
 print('Starting',label,flush=True)
 for stage,args in steps:
  assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==source
  assert not subprocess.check_output(['git','status','--porcelain'],text=True)
  commands.append({'profile':label,'stage':stage,'argv':pre+args})
  (r/'qualification-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  with (r/(label+'-'+stage+'.log')).open('w') as out:p=subprocess.run(pre+args,stdout=out,stderr=subprocess.STDOUT)
  if p.returncode:raise SystemExit(label+' '+stage+' failed; original log retained')
 assert '100% tests passed, 0 tests failed out of 9' in (r/(label+'-tests.log')).read_text()
 shutil.copyfile(b/'Testing/Temporary/LastTest.log',r/(label+'-LastTest.log'))
 records[label]={n:hashlib.sha256((b/n).read_bytes()).hexdigest()for n in targets}
 (r/'qualified-profiles.json').write_text(json.dumps({'source':source,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'targets':targets,'selector':selector,'executableHashes':records,'nativeAudioQualified':False},indent=2)+'\n')
 print('Passed',label,flush=True)
