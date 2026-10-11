from pathlib import Path
import subprocess,json,hashlib,shutil
r=Path('.cache/manual-native-failure');source=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert source=='6b6d6838de7dd04196d62a3c3a6ced1fd97ba25b';assert subprocess.check_output(['git','status','--porcelain'],text=True)==''
pre=['nice','-n','15','ionice','-c','2','-n','7'];targets=['sc-manual-recording-tests','sc-manual-punch-tests','sc-capture-tests','sc-recording-tests','sc-writer-timing-tests','sc-monitor-recording-tests'];selector='^(manual-recording-control|manual-punch-owner|capture-transport|recording-recovery|writer-phase-timing|combined-monitor-recording)$';records={}
for label,ctype,flags,link in [('release','Release','',''),('sanitizer','Debug','-fsanitize=address,undefined -fno-omit-frame-pointer','-fsanitize=address,undefined'),('tsan','Debug','-fsanitize=thread -fno-omit-frame-pointer -O1','-fsanitize=thread')]:
 b=r/('build-'+label);commands=[('configure',['cmake','-S','.','-B',str(b),'-G','Ninja','-DCMAKE_BUILD_TYPE='+ctype,'-DSC_BUILD_DESKTOP=OFF','-DSC_BUILD_MEDIA=ON','-DCMAKE_CXX_FLAGS='+flags,'-DCMAKE_EXE_LINKER_FLAGS='+link]),('build',['cmake','--build',str(b),'--parallel','1','--target',*targets]),('tests',['ctest','--test-dir',str(b),'-R',selector,'--output-on-failure','--no-tests=error'])]
 print('Starting',label,flush=True)
 for stage,args in commands:
  with (r/(label+'-'+stage+'.log')).open('w') as out:p=subprocess.run(pre+args,stdout=out,stderr=subprocess.STDOUT)
  if p.returncode:raise SystemExit(label+' '+stage+' failed; keep original log')
 assert '100% tests passed, 0 tests failed out of 6' in (r/(label+'-tests.log')).read_text()
 shutil.copyfile(b/'Testing/Temporary/LastTest.log',r/(label+'-LastTest.log'))
 records[label]={n:hashlib.sha256((b/n).read_bytes()).hexdigest()for n in targets};print('Passed',label,flush=True)
(r/'qualified-profiles.json').write_text(json.dumps({'source':source,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'targets':targets,'selector':selector,'executableHashes':records,'nativeAudioQualified':False},indent=2)+'\n')
for name,unit in [('fixture','tests/manual_recording_tests.cpp')]:
 with (r/('windows-'+name+'-compile.log')).open('w') as out:p=subprocess.run(pre+['x86_64-w64-mingw32-g++','-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Iinclude','-Ithird_party','-Ithird_party/libsndfile','-c',unit,'-o',str(r/('windows-'+name+'.obj'))],stdout=out,stderr=subprocess.STDOUT)
 if p.returncode:raise SystemExit('Windows fixture compilation failed; original log retained')
print('Windows fixture compile passed; no execution claim',flush=True)
