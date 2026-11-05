# SPDX-License-Identifier: GPL-3.0-only
"""Actual child-process handshake; expected media bytes/hashes are independent."""
import copy, hashlib, json, os, queue, struct, subprocess, sys, tempfile, threading, uuid
from pathlib import Path
worker,probe,fixture_worker=map(lambda p:str(Path(p).resolve()),sys.argv[1:4])
checks=0;cases=0;live=[]
base=Path(tempfile.gettempdir())/'sc-restore-Κиїв-'/str(uuid.uuid4());base.mkdir(parents=True)
def check(good,message):
    global checks
    checks+=1
    if not good:raise AssertionError(message)
def encoded(value):return json.dumps(value,ensure_ascii=True,separators=(',',':')).encode('ascii')
def hx(value):return value.encode('utf-8').hex()
def wave():return b'RIFF'+struct.pack('<I',44)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,48000,96000,2,16)+b'data'+struct.pack('<Ihhhh',8,0,1,32767,-32768)
def setup():
    global cases
    cases+=1;case=base/str(uuid.uuid4());source=case/'input';target=case/'project-été-Κиїв';source.mkdir(parents=True);target.mkdir()
    data=wave();leaf='source-été-Κиїв.wav';(source/leaf).write_bytes(data);manifest=encoded({'sentinel':'Ελληνικά 日本語','case':cases});(target/'project.json').write_bytes(manifest)
    child=subprocess.Popen([probe,'--desktop-file','--root',str(source),'--relative',leaf,'--maximum-bytes','1024'],stdout=subprocess.PIPE,stderr=subprocess.PIPE);live.append(child)
    out,err=child.communicate(timeout=10);check(child.returncode==0 and not err,'Real checking process failed');report=json.loads(out)
    check(report['workerPid']==child.pid and report['sourceSha256']==hashlib.sha256(data).hexdigest(),'Checker identity/content not independently bound')
    record={'schema':'sc-asset-restoration-v1','phase':1,'operation':str(uuid.uuid4()),'asset':str(uuid.uuid4()),'projectEpoch':11,'modelRevision':0,'request':21,
            'relativePathHex':hx('media/été-Κиїв/nested/take.wav'),'sourceSha256':hashlib.sha256(data).hexdigest(),'sourceBytes':len(data),'frames':4,'rateHz':48000,'channels':1,'layoutKind':0,'sourceRootsPersisted':False}
    request={'protocol':'sc-restoration-worker-v1','project':str(uuid.uuid4()),'rootHex':hx(str(target)),'sourceRootHex':hx(str(source)),'sourceLeafHex':hx(leaf),
             'recordHex':encoded(record).hex(),'checkReportHex':out.strip().hex(),'checkPid':child.pid,'checkJob':31,'dispatchJob':41,'autosaveToken':51,'maximumBytes':1024}
    return {'case':case,'target':target,'source':source/leaf,'data':data,'manifest':manifest,'record':record,'request':request}
def start(c,raw=None,program=worker,args=()):
    raw=encoded(c['request']) if raw is None else raw;(c['case']/'request.bin').write_bytes(raw)
    p=subprocess.Popen([program,*args],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE);live.append(p)
    try:p.stdin.write(raw+b'\n');p.stdin.flush()
    except BrokenPipeError:pass
    return p
def read_ready(p,c,canonical_absent=True):
    result=queue.Queue()
    def read():
        try:result.put(p.stdout.readline())
        except Exception as e:result.put(e)
    thread=threading.Thread(target=read,daemon=True);thread.start()
    try:line=result.get(timeout=10)
    except queue.Empty:
        p.kill();p.wait(timeout=10);thread.join(timeout=1);raise AssertionError('Worker READY deadline exceeded')
    thread.join(timeout=1);check(not thread.is_alive(),'READY reader did not retire')
    if isinstance(line,Exception):raise line
    (c['case']/'ready.bin').write_bytes(line)
    if not line:return None
    j=json.loads(line);check(set(j)=={'protocol','workerPid','operation','recordHex'} and j['protocol']=='sc-restoration-ready-v1','READY schema differs')
    check(j['workerPid']==p.pid and j['operation']==c['record']['operation'],'READY child/operation mismatch')
    check(json.loads(bytes.fromhex(j['recordHex']))==c['record'],'READY plan changed')
    if canonical_absent:check(not (c['target']/'media').exists(),'Preparation published canonical media')
    return j
def permit(c,p):
    r=c['record'];q=c['request']
    return {'protocol':'sc-restoration-permit-v1','phase':'publish','project':q['project'],'operation':r['operation'],'workerPid':p.pid,'asset':r['asset'],
            'sourceBytes':r['sourceBytes'],'sourceSha256':r['sourceSha256'],'frames':r['frames'],'rateHz':r['rateHz'],'channels':r['channels'],'layoutKind':r['layoutKind'],
            'relativePathHex':r['relativePathHex'],'rootHex':q['rootHex'],'projectEpoch':r['projectEpoch'],'modelRevision':r['modelRevision'],'request':r['request'],
            'checkJob':q['checkJob'],'dispatchJob':q['dispatchJob'],'autosaveToken':q['autosaveToken']}
def finish(p,c,wire=None):
    if p.stdin:
        try:
            if wire is not None:p.stdin.write(wire+b'\n');p.stdin.flush()
        except BrokenPipeError:pass
        try:p.stdin.close()
        except BrokenPipeError:pass
        p.stdin=None
    out,err=p.communicate(timeout=10);(c['case']/'terminal.bin').write_bytes(out);(c['case']/'stderr.bin').write_bytes(err)
    check((c['target']/'project.json').read_bytes()==c['manifest'],'Worker changed accepted manifest bytes')
    return p.returncode,out,err
def canonical(c):return c['target']/bytes.fromhex(c['record']['relativePathHex']).decode('utf-8')
def refused(p,c,wire=None,was_ready=True):
    code,out,err=finish(p,c,wire);check(code!=0 and not out and err,'Refused worker claimed success')
    j=json.loads(err);check(j['protocol']=='sc-restoration-worker-error-v1' and j['workerPid']==p.pid,'Refusal diagnostic differs')
    check(not canonical(c).exists(),'Refused operation published canonical media')
    if was_ready:
        op=c['target']/c['record']['operation'];check((op/'media.partial').read_bytes()==c['data'],'Refusal removed staged bytes')
        check(not (op/'receipt.json').exists(),'Refusal fabricated final receipt')
try:
    c=setup();p=start(c);check(read_ready(p,c) is not None,'No READY');code,out,err=finish(p,c,encoded(permit(c,p)))
    check(code==0 and not err,'Permitted publication failed');result=json.loads(out)
    check(set(result)=={'protocol','workerPid','operation','published','receiptPublished','postCommitFlushFailed','durability','recordHex'} and result['protocol']=='sc-restoration-result-v1','Terminal schema differs')
    check(result['workerPid']==p.pid and result['operation']==c['record']['operation'] and result['published'] and result['receiptPublished'] and not result['postCommitFlushFailed'],'Publication receipt differs')
    check(result['durability']==(1 if os.name=='nt' else 2),'Native durability overstated')
    check(canonical(c).read_bytes()==c['data'] and c['source'].read_bytes()==c['data'],'Full exact media bytes changed')
    op=c['target']/c['record']['operation'];published=copy.deepcopy(c['record']);published['phase']=2
    check(json.loads((op/'intent.json').read_bytes())==c['record'] and json.loads((op/'receipt.json').read_bytes())==published,'Durable intent/receipt changed')
    check(json.loads(bytes.fromhex(result['recordHex']))==published,'Terminal record changed')
    # EOF is cancellation before permission. A killed prepared child also retires handles.
    c=setup();p=start(c);read_ready(p,c);refused(p,c)
    c=setup();p=start(c);read_ready(p,c);p.kill();code,out,err=finish(p,c);check(code!=0 and not canonical(c).exists(),'Killed prepared child published')
    check((c['target']/c['record']['operation']/'media.partial').read_bytes()==c['data'],'Killed child lost inspectable staging')
    # Every permission mismatch remains before the canonical rename.
    for field in ['workerPid','dispatchJob','autosaveToken','operation','rootHex','sourceSha256']:
        c=setup();p=start(c);read_ready(p,c);j=permit(c,p);j[field]=j[field]+1 if isinstance(j[field],int) else j[field]+'x';refused(p,c,encoded(j))
    c=setup();p=start(c);read_ready(p,c);wire=encoded(permit(c,p));refused(p,c,b'{"phase":"publish",'+wire[1:])
    c=setup();p=start(c);read_ready(p,c);refused(p,c,encoded(permit(c,p))+b'\n{}')
    # Existing and racing destination names are never overwritten.
    for race in [False,True]:
        c=setup()
        if not race:canonical(c).parent.mkdir(parents=True);canonical(c).write_bytes(b'winner')
        p=start(c);check(read_ready(p,c,canonical_absent=race) is not None,'No destination-case READY')
        if race:canonical(c).parent.mkdir(parents=True);canonical(c).write_bytes(b'winner')
        code,out,err=finish(p,c,encoded(permit(c,p)));check(code!=0 and not out and canonical(c).read_bytes()==b'winner','Existing/racing target overwritten')
    # Invalid request/schema/context fails before destination staging.
    for mode in range(8):
        c=setup();j=c['request'];raw=None
        if mode==0:j['unknown']=True
        if mode==1:del j['autosaveToken']
        if mode==2:j['checkJob']=True
        if mode==3:j['autosaveToken']=0
        if mode==4:j['maximumBytes']=2048
        if mode==5:j['checkPid']+=1
        if mode==6:raw=b'{"project":"duplicate",'+encoded(j)[1:]
        if mode==7:raw=b' '*65537
        p=start(c,raw,args=('--maximum-source-bytes','1024'));check(read_ready(p,c) is None,'Invalid request reached READY');refused(p,c,was_ready=False)
        check(set(x.name for x in c['target'].iterdir())=={'project.json'},'Invalid request created destination staging')
    # Prior checked data does not qualify changed source bytes.
    c=setup();c['source'].write_bytes(c['data'][:-1]+bytes([c['data'][-1]^1]));p=start(c);check(read_ready(p,c) is None,'Changed source reached READY');refused(p,c,was_ready=False)
    check(set(x.name for x in c['target'].iterdir())=={'project.json'},'Changed source created staging')
    # Adversarial, syntactically valid forged check claims cannot bypass finite decoding.
    c=setup();data=b'RIFF'+struct.pack('<I',44)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,3,1,48000,192000,4,32)+b'data'+struct.pack('<III',8,0x7fc00000,0)
    c['source'].write_bytes(data);c['data']=data;c['record']['sourceSha256']=hashlib.sha256(data).hexdigest();c['record']['frames']=2
    forged=json.loads(bytes.fromhex(c['request']['checkReportHex']));forged.update(sourceSha256=c['record']['sourceSha256'],frames=2,decodedFrames=2,encodingId=5,bitsPerSample=32,peakLinear=0)
    c['request']['recordHex']=encoded(c['record']).hex();c['request']['checkReportHex']=encoded(forged).hex();p=start(c);check(read_ready(p,c) is None,'Forged finite report reached READY');refused(p,c,was_ready=False)
    check(set(x.name for x in c['target'].iterdir())=={'project.json'},'Nonfinite source created staging')
    # Production has no qualification crash switch. The separate test binary proves
    # an absent terminal reply can coexist with exact published canonical bytes.
    c=setup();p=start(c,args=('--qualify-crash-after-publish',));check(read_ready(p,c) is None,'Production accepted qualification switch');refused(p,c,was_ready=False)
    c=setup();p=start(c,program=fixture_worker,args=('--qualify-crash-after-publish',));read_ready(p,c);code,out,err=finish(p,c,encoded(permit(c,p)))
    check(code==71 and not out and canonical(c).read_bytes()==c['data'],'Post-rename process crash lost exact media truth')
    op=c['target']/c['record']['operation'];check((op/'receipt.partial').exists() and not (op/'receipt.json').exists(),'Crash evidence incorrectly finalized')
    check(all(p.poll() is not None for p in live),'Owned child remained live')
    print(json.dumps({'checks':checks,'cases':cases,'actualProcesses':len(live),'independentWholeBytes':True,'withoutPermitNoPublication':True,'postRenameCrashNeedsReconciliation':True,'fullDesktopWorkflow':False}))
    import shutil;shutil.rmtree(base)
except BaseException:
    for p in live:
        if p.poll() is None:p.kill();p.wait(timeout=10)
    sys.stdout.buffer.write(('Retained restoration fixture '+str(base)+'\n').encode('utf-8'));sys.stdout.flush();raise
