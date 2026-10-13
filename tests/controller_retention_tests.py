# SPDX-License-Identifier: GPL-3.0-only
import importlib.util,json,tempfile,uuid
from pathlib import Path
spec=importlib.util.spec_from_file_location('collector',Path(__file__).resolve().parents[1]/'.github/scripts/retain-controller-failure-projects.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
with tempfile.TemporaryDirectory() as work:
    build=Path(work);logs=build/'Testing/Temporary';logs.mkdir(parents=True);log=logs/'LastTest.log'
    parent=Path(tempfile.gettempdir())/'sc-pw-controller-é-';parent.mkdir(exist_ok=True)
    source=parent/str(uuid.uuid4());source.mkdir();extras=[]
    try:
        (source/'media').mkdir();(source/'media/take.wav').write_bytes(bytes([0,255,17,0]));(source/'project.json').write_text('{"title":"Ελληνικά 日本語"}',encoding='utf-8')
        log.write_text(c.MARKER+str(source)+'\n',encoding='utf-8');receipt=c.retain(build);assert len(receipt)==1 and len(receipt[0]['originalFiles'])==2
        target=build/'controller-failure-projects'/source.name
        assert (target/'media/take.wav').read_bytes()==(source/'media/take.wav').read_bytes()
        assert json.loads((target/'retention.json').read_text(encoding='utf-8'))['atomicProjectSnapshot'] is False
        def refuses():
            try:c.retain(build)
            except ValueError:return
            raise AssertionError('Invalid retention accepted')
        refuses() # Existing snapshot is never overwritten.
        log.write_text(c.MARKER+str(build)+'\n',encoding='utf-8');refuses()
        log.write_text(c.MARKER+str(source)+'\n',encoding='utf-8')
        linked=False
        try:(source/'escape').symlink_to(build,target_is_directory=True);linked=True
        except OSError:
            if not c.os.name=='nt':raise
        if linked:refuses();(source/'escape').unlink()
        log.write_text(c.MARKER+str(parent/'not-a-uuid')+'\n',encoding='utf-8');refuses()
        with log.open('wb') as out:out.truncate(16*1024*1024+1)
        refuses()
        for _ in range(3):
            item=parent/str(uuid.uuid4());item.mkdir();extras.append(item)
        log.write_text(''.join(c.MARKER+str(item)+'\n' for item in [source,*extras]),encoding='utf-8');refuses()
        log.write_text(c.MARKER+str(source)+'\n',encoding='utf-8')
        oversized=source/'oversized';
        with oversized.open('wb') as out:out.truncate(512*1024*1024+1)
        refuses();oversized.unlink()
        log.write_text('No failure markers\n',encoding='utf-8');assert c.retain(build)==[]
        print('Controller retention: exact Unicode/audio bytes, foreign path/no-overwrite, link refusal='+str(linked))
    finally:
        import shutil
        shutil.rmtree(source)
        for item in extras:shutil.rmtree(item)
