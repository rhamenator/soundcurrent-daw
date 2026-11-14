# SPDX-License-Identifier: GPL-3.0-only
import importlib.util,json,tempfile,uuid,subprocess,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('collector',Path(__file__).resolve().parents[1]/'.github/scripts/retain-controller-failure-projects.py')
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
with tempfile.TemporaryDirectory() as work:
    build=Path(work);logs=build/'Testing/Temporary';logs.mkdir(parents=True);log=logs/'LastTest.log'
    parent=Path(tempfile.gettempdir())/'sc-pw-controller-é-';parent.mkdir(exist_ok=True)
    source=parent/str(uuid.uuid4());source.mkdir();extras=[]
    try:
        (source/'media').mkdir();(source/'media/take.wav').write_bytes(bytes([0,255,17,0]));(source/'project.json').write_text('{"title":"Ελληνικά 日本語"}',encoding='utf-8')
        log.write_bytes(b'Unrelated legacy-code-page diagnostic: \xe9\xff\n'+(c.MARKER+str(source)+'\n').encode('utf-8'));originalLog=log.read_bytes()
        receipt=c.retain(build);assert len(receipt)==1 and len(receipt[0]['originalFiles'])==2 and log.read_bytes()==originalLog
        target=build/'controller-failure-projects'/('controller-'+source.name)
        assert (target/'media/take.wav').read_bytes()==(source/'media/take.wav').read_bytes()
        assert json.loads((target/'retention.json').read_text(encoding='utf-8'))['atomicProjectSnapshot'] is False
        def refuses():
            try:c.retain(build)
            except ValueError:return
            raise AssertionError('Invalid retention accepted')
        refuses() # Existing snapshot is never overwritten.
        import shutil
        shutil.rmtree(build/'controller-failure-projects')
        log.write_bytes(c.MARKER.encode('ascii')+b'invalid-path-\xe9\n');refuses()
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
        oversized=source/'oversized'
        with oversized.open('wb') as out:out.truncate(512*1024*1024+1)
        refuses();oversized.unlink()
        log.write_text('No failure markers\n',encoding='utf-8');assert c.retain(build)==[]
        panelParent=Path(tempfile.gettempdir())/c.POLICIES[c.PANEL_MARKER];panelParent.mkdir(exist_ok=True)
        panel=panelParent/str(uuid.uuid4());panel.mkdir();extras.append(panel)
        (panel/'devices-é').mkdir();(panel/'devices-é/result.json').write_text('{"complete":false}',encoding='utf-8')
        log.write_text(c.PANEL_MARKER+str(panel)+'\n',encoding='utf-8');assert len(c.retain(build))==1
        assert (build/'controller-failure-projects'/('panel-'+panel.name)/'devices-é/result.json').read_bytes()==(panel/'devices-é/result.json').read_bytes()
        parentRoot=Path(tempfile.gettempdir())/c.POLICIES[c.PARENT_MARKER];parentRoot.mkdir(exist_ok=True)
        interruption=parentRoot/str(uuid.uuid4());interruption.mkdir();extras.append(interruption)
        project=interruption/'during-processing'/'project-été-Κиїв';project.mkdir(parents=True)
        (project/'audio.partial').write_bytes(bytes([0,255,17,0]))
        (project/'.save.lock').write_bytes(b'')
        (interruption/'failure-report.json').write_text('{"failure":"5"}',encoding='utf-8')
        log.write_bytes((c.PARENT_MARKER+str(interruption)+'\n').encode('utf-8'))
        parentReceipt=c.retain(build);assert len(parentReceipt)==1 and len(parentReceipt[0]['originalFiles'])==3
        saved=build/'controller-failure-projects'/('parent-'+interruption.name)
        assert (saved/'during-processing/project-été-Κиїв/audio.partial').read_bytes()==bytes([0,255,17,0])
        assert (saved/'during-processing/project-été-Κиїв/.save.lock').read_bytes()==b''
        refuses() # The parent snapshot also forbids overwrite.
        log.write_bytes((c.PARENT_MARKER+str(source)+'\n').encode('utf-8'));refuses()
        log.write_bytes((c.PARENT_MARKER+str(parentRoot/'not-a-uuid')+'\n').encode('utf-8'));refuses()
        restorationRoot=Path(tempfile.gettempdir())/c.POLICIES[c.RESTORATION_MARKER];restorationRoot.mkdir(exist_ok=True)
        restoration=restorationRoot/str(uuid.uuid4());restoration.mkdir();extras.append(restoration)
        operation=restoration/str(uuid.uuid4());operation.mkdir()
        (operation/'intent.json').write_bytes(b'{"phase":1}')
        (operation/'receipt.partial').write_bytes(b'{"phase":2}')
        (restoration/'media').mkdir();(restoration/'media/take-été.wav').write_bytes(bytes([0,255,17,0]))
        log.write_bytes((c.RESTORATION_MARKER+str(restoration)+'\n').encode('utf-8'))
        restorationReceipt=c.retain(build);assert len(restorationReceipt)==1 and len(restorationReceipt[0]['originalFiles'])==3
        saved=build/'controller-failure-projects'/('restoration-'+restoration.name)
        assert (saved/'media/take-été.wav').read_bytes()==bytes([0,255,17,0])
        assert (saved/operation.name/'intent.json').read_bytes()==b'{"phase":1}'
        assert (saved/operation.name/'receipt.partial').read_bytes()==b'{"phase":2}'
        refuses()
        log.write_bytes((c.RESTORATION_MARKER+str(source)+'\n').encode('utf-8'));refuses()
        # Actual producer failure: matching catalog audit with a changed source
        # fingerprint must publish a bounded marker and retain exact inputs.
        repository=Path(__file__).resolve().parents[1]
        sandbox=build/'catalog-source';(sandbox/'tests').mkdir(parents=True);(sandbox/'tools').mkdir()
        for folder in ['ui','localization']:shutil.copytree(repository/folder,sandbox/folder)
        for relative in ['tests/translation_catalog_tests.py','tools/localization.py']:
            shutil.copy2(repository/relative,sandbox/relative)
        changed=sandbox/'ui/wave_check_controller.cpp'
        with changed.open('ab') as out:out.write(b'\n// retention qualification: deliberately stale fingerprint\n')
        expected=changed.read_bytes()
        child=subprocess.run([sys.executable,str(sandbox/'tests/translation_catalog_tests.py')],capture_output=True,timeout=30)
        assert child.returncode!=0 and b'UI changed' in child.stderr
        markers=[line for line in child.stdout.splitlines() if line.startswith(c.LOCALIZATION_MARKER.encode('ascii'))]
        assert len(markers)==1
        generated=Path(markers[0][len(c.LOCALIZATION_MARKER):].decode('utf-8'));extras.append(generated)
        log.write_bytes(child.stdout+child.stderr)
        receipts=c.retain(build);assert len(receipts)==1
        saved=build/'controller-failure-projects'/('localization-'+generated.name)
        assert (saved/'fixture/ui/wave_check_controller.cpp').read_bytes()==expected
        assert (saved/'fixture/localization/source-inventory.json').read_bytes()==(sandbox/'localization/source-inventory.json').read_bytes()
        assert json.loads((saved/'failure.json').read_text(encoding='utf-8'))['case']=='matching'
        assert (generated/'fixture/ui/wave_check_controller.cpp').read_bytes()==expected
        refuses()
        log.write_bytes((c.LOCALIZATION_MARKER+str(source)+'\n').encode('utf-8'));refuses()
        for n in range(16):(logs/('extra-'+str(n)+'.log')).write_text('',encoding='utf-8')
        refuses()
        print('Controller retention: exact Unicode/audio and parent partial/hidden bytes, foreign path/no-overwrite, link refusal='+str(linked))
    finally:
        shutil.rmtree(source)
        for item in extras:shutil.rmtree(item)
