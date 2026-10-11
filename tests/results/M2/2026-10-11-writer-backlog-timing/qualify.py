from pathlib import Path
import subprocess,json,hashlib,shlex,shutil
r=Path('.cache/writer-backlog-timing');head='3ed02b44b206250a2726cefb59d2190c1e33cc9e';prior=Path('.cache/native-channel-state');q=json.loads((prior/'qualified-profiles.json').read_text());old=json.loads((prior/'frozen-inputs.json').read_text());prefix=['nice','-n','15','ionice','-c','2','-n','7'];commands=[];profiles={}
assert q['source']==old['source']=='9d5da1c5889dc57af20fe70408a43932fcee6f61'
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head and not subprocess.check_output(['git','status','--porcelain'],text=True)
for x in old['selectedInputs']:
 if x['path']=='tests/manual_recording_tests.cpp':continue
 assert subprocess.check_output(['git','rev-parse','HEAD:'+x['path']],text=True).strip()==x['gitBlob'];assert hashlib.sha256(Path(x['path']).read_bytes()).hexdigest()==x['checkoutSha256']
for label,ctype,flags,link in [('release','Release','',''),('sanitizer','Debug','-fsanitize=address,undefined -fno-omit-frame-pointer','-fsanitize=address,undefined'),('tsan','Debug','-fsanitize=thread -fno-omit-frame-pointer -O1','-fsanitize=thread')]:
 b=(r/('build-dev' if label=='release' else 'build-'+label)).resolve();archives={}
 def run(stage,argv,cwd=None):
  assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==head and not subprocess.check_output(['git','status','--porcelain'],text=True)
  command=prefix+argv;commands.append({'profile':label,'stage':stage,'cwd':str(cwd or Path.cwd()),'argv':command});(r/'qualification-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
  with (r/(label+'-'+stage+'.log')).open('w') as out:p=subprocess.run(command,cwd=cwd,stdout=out,stderr=subprocess.STDOUT)
  if p.returncode:raise SystemExit(label+' '+stage+' failed; preserve original log')
 print('Starting',label,flush=True)
 run('configure',['cmake','-S','.','-B',str(b),'-G','Ninja','-DCMAKE_BUILD_TYPE='+ctype,'-DSC_BUILD_DESKTOP=OFF','-DSC_BUILD_MEDIA=ON','-DCMAKE_CXX_FLAGS='+flags,'-DCMAKE_EXE_LINKER_FLAGS='+link])
 if label=='release':run('build',['cmake','--build',str(b),'--parallel','1','--target','sc-manual-recording-tests'])
 else:
  frozen=(prior/('build-'+label)).resolve()
  for name,sha in q['executableHashes'][label].items():assert hashlib.sha256((frozen/name).read_bytes()).hexdigest()==sha
  original=subprocess.check_output(['ninja','-C',str(b),'-t','commands','sc-manual-recording-tests'],text=True);(r/(label+'-generated-commands.txt')).write_text(original)
  compileCommands=[shlex.split(line) for line in original.splitlines() if ' -c ' in line and ('/tests/manual_recording_tests.cpp' in line or '/tests/rt_audit.cpp' in line)]
  assert len(compileCommands)==2
  for number,argv in enumerate(compileCommands):
   assert argv[0]=='/usr/bin/c++' and '-DSC_WRAP_LIBC' in argv and '-std=c++20' in argv
   obj=Path(argv[argv.index('-o')+1]);(b/obj).parent.mkdir(parents=True,exist_ok=True)
   run('compile-'+str(number),argv,b)
  argv=shlex.split(original.splitlines()[-1]);assert argv[:2]==[':','&&'] and argv[-2:]==['&&',':'];argv=argv[2:-2]
  assert argv[0]=='/usr/bin/c++' and '-Wl,--wrap=pthread_mutex_lock' in argv and argv[argv.index('-o')+1]=='sc-manual-recording-tests'
  for n,a in enumerate(argv):
   if a.startswith('libsc-') and a.endswith('.a'):
    p=frozen/a;assert p.is_file();archives[a]={'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()};argv[n]=str(p)
  assert len(archives)==19
  run('link-with-readonly-production-archives',argv,b)
  for a,v in archives.items():assert hashlib.sha256((frozen/a).read_bytes()).hexdigest()==v['sha256']
 run('tests',['ctest','--test-dir',str(b),'-R','^manual-recording-control$','--output-on-failure','--no-tests=error'])
 assert '100% tests passed, 0 tests failed out of 1' in (r/(label+'-tests.log')).read_text()
 shutil.copyfile(b/'Testing/Temporary/LastTest.log',r/(label+'-LastTest.log'))
 profiles[label]={'exeSha256':hashlib.sha256((b/'sc-manual-recording-tests').read_bytes()).hexdigest(),'productionArchivesReadOnly':archives,'allOther635InputsUnchanged':True}
 (r/'qualified-profiles.json').write_text(json.dumps({'source':head,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'productionArchiveSource':old['source'],'profiles':profiles,'nativeWindowsRuntimeExecuted':False},indent=2)+'\n')
 print('Passed',label,flush=True)
