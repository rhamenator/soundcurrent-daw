from pathlib import Path
import subprocess,json,hashlib
r=Path('.cache/native-channel-state');source='9d5da1c5889dc57af20fe70408a43932fcee6f61';prefix=['nice','-n','15','ionice','-c','2','-n','7'];b=r/'build-windows'
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==source
assert not subprocess.check_output(['git','status','--porcelain'],text=True)
targets=['sc-monitor-processing-tests','sc-monitor-startup-tests','sc-native-monitor-startup-tests','sc-wasapi-monitor-render']
commands=[('configure',['cmake','-S','.','-B',str(b),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-mingw.cmake','-DCMAKE_BUILD_TYPE=Release','-DSC_BUILD_MEDIA=OFF','-DSC_BUILD_DESKTOP=OFF']),('build',['cmake','--build',str(b),'--parallel','1','--target',*targets])]
for stage,cmd in commands:
 with (r/('windows-'+stage+'.log')).open('w') as out:p=subprocess.run(prefix+cmd,stdout=out,stderr=subprocess.STDOUT)
 if p.returncode:raise SystemExit('Windows '+stage+' failed; preserve original')
objects={}
for tag,src in [('map','src/monitor_channel_map.cpp'),('processing','src/monitor_processing.cpp'),('handoff','src/monitor_startup_handoff.cpp'),('native-state','src/native_original_recording.cpp'),('native-state-fixture','tests/native_original_recording_tests.cpp'),('native-startup-fixture','tests/native_monitor_startup_tests.cpp'),('processing-fixture','tests/monitor_processing_tests.cpp')]:
 cmd=prefix+['x86_64-w64-mingw32-g++','-std=c++20','-O2','-Wall','-Wextra','-Wpedantic','-DSC_NATIVE_STARTUP_STATE','-Iinclude','-Isrc','-Ithird_party','-Ithird_party/libsndfile','-c',src,'-o',str(r/('windows-'+tag+'.obj'))]
 with (r/('windows-'+tag+'-compile.log')).open('w') as out:p=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT)
 if p.returncode:raise SystemExit('Windows '+tag+' failed; retain original')
 assert 'warning:' not in (r/('windows-'+tag+'-compile.log')).read_text()
 objects[tag]={'source':src,'command':cmd,'sha256':hashlib.sha256((r/('windows-'+tag+'.obj')).read_bytes()).hexdigest()}
(r/'windows-qualification.json').write_text(json.dumps({'source':source,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'targets':targets,'commands':commands,'objects':objects,'nativeSdkCompiled':True,'nativeSdkRuntimeExecuted':False},indent=2)+'\n');print('Seven Windows objects warning-free; SDK and three test targets compile/link',flush=True)
