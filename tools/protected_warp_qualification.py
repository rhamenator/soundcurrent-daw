# SPDX-License-Identifier: GPL-3.0-only
"""Check maintainer-observed v5 worker evidence; not authentication or audio replay."""
import re, hashlib, json
from copy import deepcopy
PROTOCOL='sc-stretch-render-v5'
PROCESSOR='soundcurrent.stretch-transient-protected-rubberband4-r3-v1'
# Frozen original GPL prototype bank: input PCM and its WAV envelope, independently
# observed identical on Linux and native Windows. Hashes bind scenario coverage;
# they are not signatures or a claim that any untrusted report is authentic.
BANK_INPUTS={
 'impulse':(2,'cc29eaeb09aac4ed58e0fb177b2aff63af7df0891862ac335fea7fd8edd89219','3ab3b2a8d0d9219453eae135e9351bb5a66b7f954bed4f3da4233ca0a4059922'),
 'attack':(8,'cae0658ceb6d286ce1dc80155f49307439099b63ca3286fbabf3bc2d8716e5f6','d2a3af1b28cbd47897864b3998c1b440754acf47cb0b39de92a43ce889838f99'),
 'long-attack':(8,'401870d1e88cbba214a4d3a06fcb47a0f361c0e95f5d98cab3a74bcd9f9dbde2','95b13d30a4bc1bd3ccd03583da83b0796e1d92488f4ec03d755b52c4c1302029'),
 'attack-bed':(8,'a88c0ab3b5c97ccec92e6b3719dfd9bdc8a87e83f43e57362c61c6cb01841b38','1b00571061f656696ed1049ba740727bdda28f634387abeb9801e948ad950e1d'),
 'sustain':(8,'d96e0f0be52277ee410bc4639df67b599ff418e864cb3a3ca464ae0ef18b0681','5d005fda33d31e1616e9c5a5ca1f915678b5e8fbbeb4acb5820a6e496f00bc3e'),
 'cancellation':(8,'e125bf762015e65911d0b00e9d5926b5a252ff5fe6fc79773887de5ba0707ddc','56e128fff5a6f2edc829923a3bf044e631673be588435763437f231cd455a957'),
}
def _bank_request(family,profile,events=None,targets=None):
    events=[4096,12288,20480,28672] if events is None else events
    targets=(events if profile=='identity' else [v*3//2 for v in events] if profile=='uniform' else [6144,16384,32768,43008]) if targets is None else targets
    target=32768 if profile=='identity' else 49152
    markers=[];spans=[];points=[{'source':[0,0,1],'output':[0,0,1]}]
    for i,(source,output) in enumerate(zip(events,targets)):
        owner=f'00000000-0000-0000-0000-{i+1:012x}'
        markers.append({'id':owner,'source':[source,0,1],'output':[output,0,1]});span={'owner':owner}
        for key,value in [('sourceBegin',source-256),('sourceAnchor',source),('sourceEnd',source+2048),('outputBegin',output-256),('outputAnchor',output),('outputEnd',output+2048)]:span[key]=[value,0,1]
        spans.append(span)
        for x,y in [(source-256,output-256),(source,output),(source+2048,output+2048)]:
            point={'source':[x,0,1],'output':[y,0,1]}
            if point!=points[-1]:points.append(point)
    last={'source':[32768,0,1],'output':[target,0,1]}
    if last!=points[-1]:points.append(last)
    warp={'mode':'transient-protected-v1','before':256,'after':2048,'halo':64,'minimumNonunityGap':64,'chunkFrames':512,'map':'soundcurrent.warp-piecewise-rational-v1','markers':markers,'spans':spans,'points':points}
    return {'protocol':PROTOCOL,'processor':PROCESSOR,'relative':'media/source.wav','sha256':BANK_INPUTS[family][2],'rate':48000,'channels':BANK_INPUTS[family][0],'sourceFrames':32768,'first':0,'firstFraction':0,'firstDenominator':1,'frames':32768,'timeNumerator':1 if profile=='identity' else 3,'timeDenominator':1 if profile=='identity' else 2,'pitchMilliCents':0,'formantPreserved':True,'contextEnabled':False,'contextBefore':0,'contextAfter':0,'warp':warp}

def qualify_protected_warp(q,head,tree,worker_sha256,platform):
    def require(value):
        if not value:raise ValueError('Protected stretch helper lacks matching native process/artifact qualification')
    def integer(value,minimum=0):return type(value) is int and value>=minimum
    def digest(value):return type(value) is str and re.fullmatch(r'[0-9a-f]{64}',value) is not None
    def identifier(value):return type(value) is str and re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}',value) is not None
    def same(a,b):
        # JSON equality must not treat booleans or floating values as integers.
        if type(a) is not type(b):return False
        if type(a) is dict:return a.keys()==b.keys() and all(same(a[k],b[k]) for k in a)
        if type(a) is list:return len(a)==len(b) and all(same(x,y) for x,y in zip(a,b))
        return a==b
    require(platform in ('linux','win32') and type(q) is dict)
    require(q.get('schema')=='soundcurrent.protected-warp-integration-evidence-v1')
    require(type(head) is str and re.fullmatch(r'[0-9a-f]{40}',head) is not None and q.get('sourceCommit')==head)
    require(type(tree) is str and re.fullmatch(r'[0-9a-f]{40}',tree) is not None and q.get('sourceTree')==tree)
    require(digest(worker_sha256) and q.get('workerSha256')==worker_sha256)
    require(digest(q.get('verifierSha256')) and digest(q.get('prototypeSha256')))
    require(type(q.get('platform')) is str and q['platform'].startswith('Windows-' if platform=='win32' else 'Linux-'))
    require(q.get('nativeAudio') is False and q.get('fullQualityQualified') is False)
    require(q.get('syntheticSourceRights')=='Original project fixtures, GPL-3.0-only')
    require(type(q.get('minimumNonunityAcousticGapFrames')) is int and q['minimumNonunityAcousticGapFrames']==2048)
    require(integer(q.get('checks'),517))
    require(type(q.get('workers')) is list and len(q['workers'])==33 and type(q.get('workerProcesses')) is int and q['workerProcesses']==33)
    require(type(q.get('completedRenders')) is int and q['completedRenders']==21)
    operations=set();completed={};refused=[]
    for child in q['workers']:
        require(type(child) is dict and integer(child.get('pid'),1) and type(child.get('exit')) is int)
        request=child.get('request');ready=child.get('ready');reports=child.get('completion')
        require(type(request) is dict and identifier(request.get('operation')) and request['operation'] not in operations)
        operations.add(request['operation']);require(request.get('protocol')==PROTOCOL and type(reports) is list and type(child.get('stderr')) is str)
        if child['exit']!=0:
            require(child['exit']==1 and not reports);refused.append(child)
            if ready is not None:require(type(ready) is dict and ready.get('event')=='ready' and ready.get('protocol')==PROTOCOL and ready.get('operation')==request['operation'] and digest(ready.get('renderKey')))
            continue
        require(not child['stderr'] and type(ready) is dict and ready.get('event')=='ready' and ready.get('protocol')==PROTOCOL and ready.get('operation')==request['operation'] and digest(ready.get('renderKey')) and len(reports)==1)
        receipt=reports[0];require(type(receipt) is dict and receipt.get('complete') is True and receipt.get('protocol')==PROTOCOL)
        require(request.get('processor')==receipt.get('processor')==PROCESSOR and receipt.get('operation')==request['operation'] and receipt.get('renderKey')==ready['renderKey'])
        for key in ('channels','rate','frames','sourceFrames','first','firstFraction','firstDenominator','timeNumerator','timeDenominator'):
            require(integer(request.get(key),0 if key in ('first','firstFraction') else 1))
        require(all(request[k]<=1000000000 for k in ('frames','sourceFrames','first')) and request['first']+request['frames']<=request['sourceFrames'] and request['timeNumerator']<=1000000 and request['timeDenominator']<=1000000)
        require(request['channels'] in (2,8) and request['rate']==48000 and request['firstFraction']==0 and request['firstDenominator']==1)
        for key in ('channels','rate','frames','sourceFrames','first','firstFraction','firstDenominator'):require(same(receipt.get(key),request[key]))
        require(type(request.get('contextBefore')) is int and request['contextBefore']==0 and type(request.get('contextAfter')) is int and request['contextAfter']==0)
        require(request.get('formantPreserved') is True and request.get('contextEnabled') is False and type(request.get('pitchMilliCents')) is int and request['pitchMilliCents']==0)
        require(receipt.get('formantPreserved') is True and type(receipt.get('pitchMilliCents')) is int and receipt['pitchMilliCents']==0)
        require(request.get('relative')=='media/source.wav' and receipt.get('relative')==request['relative'] and type(receipt.get('deadlineMilliseconds')) is int and receipt['deadlineMilliseconds']==10000 and receipt.get('durabilityMinimum')=='file-flushed')
        require(identifier(request.get('assetId')) and receipt.get('assetId')==request['assetId'])
        require(digest(request.get('sha256')) and receipt.get('sourceSha256')==request['sha256'])
        require(digest(receipt.get('audioSha256')) and digest(receipt.get('sampleSha256')))
        require(receipt.get('sourceAlgorithm')=='soundcurrent.src-positioned-best-v1')
        require(receipt.get('channelPolicy')==('mono-stereo-together' if request['channels']==2 else 'discrete-apart'))
        require(type(receipt.get('target')) is int and receipt['target']==(request['frames']*request['timeNumerator']+request['timeDenominator']-1)//request['timeDenominator'])
        require(type(receipt.get('writtenFrames')) is int and receipt['writtenFrames']==receipt['target'])
        require(receipt.get('memoryMetric')==('process-commit' if platform=='win32' else 'address-space'))
        require(type(receipt.get('memoryCeilingBytes')) is int and receipt['memoryCeilingBytes']==268435456 and integer(receipt.get('payloadPeakBytes'),1) and receipt['payloadPeakBytes']<receipt['memoryCeilingBytes'])
        warp=request.get('warp');require(type(warp) is dict and same(warp,receipt.get('warp')))
        require(warp.get('mode')=='transient-protected-v1' and warp.get('map')=='soundcurrent.warp-piecewise-rational-v1')
        for key,value in [('before',256),('after',2048),('halo',64),('minimumNonunityGap',64),('chunkFrames',512)]:require(type(warp.get(key)) is int and warp[key]==value)
        for key in ('markers','spans','points'):require(type(warp.get(key)) is list and len(warp[key])>0)
        require(len(warp['markers'])==len(warp['spans']) and len(warp['markers'])<=1023)
        points=[{'source':[0,0,1],'output':[0,0,1]}];owners=set();previous=-1
        for marker,span in zip(warp['markers'],warp['spans']):
            require(type(marker) is dict and set(marker)=={'id','source','output'} and identifier(marker.get('id')) and marker['id'] not in owners)
            owners.add(marker['id'])
            for key in ('source','output'):require(type(marker[key]) is list and len(marker[key])==3 and all(type(v) is int for v in marker[key]) and marker[key][1:]==[0,1])
            source=marker['source'][0];output=marker['output'][0];require(source>previous);previous=source
            expected={'owner':marker['id']}
            for name,value in [('sourceBegin',source-256),('sourceAnchor',source),('sourceEnd',source+2048),('outputBegin',output-256),('outputAnchor',output),('outputEnd',output+2048)]:expected[name]=[value,0,1]
            require(same(span,expected))
            for source_point,output_point in [(source-256,output-256),(source,output),(source+2048,output+2048)]:
                point={'source':[source_point,0,1],'output':[output_point,0,1]}
                if not same(point,points[-1]):points.append(point)
        last={'source':[request['frames'],0,1],'output':[receipt['target'],0,1]}
        if not same(last,points[-1]):points.append(last)
        require(same(points,warp['points']))
        identity={key:receipt[key] for key in ('processor','sourceSha256','rate','channels','first','firstFraction','firstDenominator','sourceAlgorithm','frames','target','pitchMilliCents','formantPreserved','channelPolicy','warp')}
        key_hash=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf-8')).hexdigest()
        require(key_hash==ready['renderKey'])
        completed[request['operation']]=receipt
    require(len(completed)==21 and len(refused)==12)
    require(sum(child['ready'] is not None for child in refused)==(1 if platform=='win32' else 2))
    mutation=q.get('sourceMutation');require(type(mutation) is dict and mutation.get('changedRawTiming')==('before-read' if platform=='win32' else 'after-ready'))
    if platform=='win32':
        denied=mutation.get('heldReadWriteDenied');require(type(denied) is dict and type(denied.get('errno')) is int and denied['errno']==13 and denied.get('rawUnchanged') is True)
    # Each refusal must carry the intended malformed input and exact typed
    # failure/publication packet. A collection of unrelated exit-1 jobs is not
    # evidence of the claimed validation, cancellation and mutation coverage.
    base=_bank_request('cancellation','nonuniform')
    refusal_cases=[('short-gap',_bank_request('cancellation','nonuniform',[320],[384]),2,False),('canceled',base,7,True),('changed-source',base,6,platform=='linux')]
    for mode,code in enumerate([3,3,3,2,2,2,3,3,3]):
        invalid=deepcopy(base)
        if mode==0:invalid['warp']['spans'][0]['sourceBegin'][0]+=1
        if mode==1:invalid['warp']['points'][0]['source'][0]=0.0
        if mode==2:invalid['warp']['markers'][1]['id']=invalid['warp']['markers'][0]['id']
        if mode==3:invalid['firstFraction']=1;invalid['firstDenominator']=2
        if mode==4:invalid['pitchMilliCents']=1
        if mode==5:invalid['contextEnabled']=True
        if mode==6:invalid['warp']['mode']='unknown'
        if mode==7:invalid['warp']['halo']=0
        if mode==8:invalid['warp']['markers'][0]['source'][2]=0
        refusal_cases.append(('malformed-'+str(mode),invalid,code,False))
    seen_refusals=set()
    def unique_object(pairs):
        out={}
        for key,value in pairs:
            require(key not in out);out[key]=value
        return out
    for child in refused:
        request=child['request'];require(identifier(request.get('assetId')))
        body={k:v for k,v in request.items() if k not in ('operation','assetId')}
        error=json.loads(child['stderr'],object_pairs_hook=unique_object)
        require(type(error) is dict and type(error.get('errorCode')) is int)
        packet={'complete':False,'errorCode':error['errorCode'],'messageId':'stretch.render_failed','protocol':PROTOCOL,'publicationMayHaveCommitted':False}
        require(same(error,packet))
        cases=[name for name,invalid,code,ready in refusal_cases if same(body,invalid) and error['errorCode']==code and (child['ready'] is not None)==ready]
        require(len(cases)==1 and cases[0] not in seen_refusals);seen_refusals.add(cases[0])
    require(len(seen_refusals)==12)
    requests={child['request']['operation']:child['request'] for child in q['workers']}
    comparisons=q.get('comparisons');require(type(comparisons) is list and len(comparisons)==18)
    expected={(family,profile) for family in ['impulse','attack','long-attack','attack-bed','sustain','cancellation'] for profile in ['identity','uniform','nonuniform']};observed=set();compared_operations=set()
    for comparison in comparisons:
        require(type(comparison) is dict)
        require(type(comparison.get('family')) is str and type(comparison.get('profile')) is str)
        pair=(comparison.get('family'),comparison.get('profile'));require(pair in expected and pair not in observed);observed.add(pair)
        operation=comparison.get('operation');require(identifier(operation) and operation in completed and operation not in compared_operations);compared_operations.add(operation)
        receipt=completed[operation];request=requests[operation]
        body={k:v for k,v in request.items() if k not in ('operation','assetId')}
        require(same(body,_bank_request(*pair)) and comparison.get('sourcePcmSha256')==BANK_INPUTS[pair[0]][1])
        prototype=comparison.get('prototypeReport');require(type(prototype) is dict)
        require(same(prototype.get('request'),{'family':pair[0],'profile':pair[1]}))
        require(prototype.get('format')=='sc-protected-warp-render-v1' and prototype.get('nativeAudio') is False and prototype.get('fullQualityQualified') is False and prototype.get('shippingAdopted') is False)
        for key,value in [('channels',request['channels']),('frames',32768),('target',receipt['target']),('writtenFrames',receipt['target']),('beforeFrames',256),('afterFrames',2048),('haloFrames',64)]:require(same(prototype.get(key),value))
        require(same(prototype.get('points'),[{'source':p['source'][0],'output':p['output'][0]} for p in request['warp']['points']]))
        expected_spans=[];expected_gaps=[];sb=ob=0
        for index,span in enumerate(request['warp']['spans']):
            flat={k:(v if k=='owner' else v[0]) for k,v in span.items()}
            for name,edge,delta in [('coreSourceBegin','sourceBegin',64),('coreSourceEnd','sourceEnd',-64),('coreOutputBegin','outputBegin',64),('coreOutputEnd','outputEnd',-64)]:flat[name]=flat[edge]+delta
            expected_spans.append(flat)
            se=flat['sourceBegin'];oe=flat['outputBegin'];before=64 if index else 0
            expected_gaps.append({'index':index,'sourceBegin':sb,'sourceEnd':se,'outputBegin':ob,'outputEnd':oe,'contextBefore':before,'contextAfter':64,'inputFrames':se-sb+before+64,'outputFrames':oe-ob+before+64,'unityCopy':se-sb==oe-ob})
            sb=flat['sourceEnd'];ob=flat['outputEnd']
        expected_gaps.append({'index':len(expected_spans),'sourceBegin':sb,'sourceEnd':32768,'outputBegin':ob,'outputEnd':receipt['target'],'contextBefore':64,'contextAfter':0,'inputFrames':32768-sb+64,'outputFrames':receipt['target']-ob+64,'unityCopy':32768-sb==receipt['target']-ob})
        require(same(prototype.get('spans'),expected_spans) and same(prototype.get('gaps'),expected_gaps))
        require(comparison.get('allPcmEqual') is True and digest(comparison.get('sourcePcmSha256')) and digest(comparison.get('prototypePcmSha256')) and comparison.get('workerPcmSha256')==comparison['prototypePcmSha256']==receipt['sampleSha256'])
        require(type(comparison.get('frames')) is int and comparison['frames']==receipt['target'] and type(comparison.get('channels')) is int and comparison['channels']==receipt['channels'])
    require(observed==expected)
    return {'protocol':PROTOCOL,'workerProcesses':33,'completedRenders':21,'fullPcmComparisons':18,'checks':q['checks'],'nativeAudio':False,'fullQualityQualified':False}
