# SPDX-License-Identifier: GPL-3.0-only
"""Check maintainer-observed v5 worker evidence; not authentication or audio replay."""
import re, hashlib, json
PROTOCOL='sc-stretch-render-v5'
PROCESSOR='soundcurrent.stretch-transient-protected-rubberband4-r3-v1'
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
    comparisons=q.get('comparisons');require(type(comparisons) is list and len(comparisons)==18)
    expected={(family,profile) for family in ['impulse','attack','long-attack','attack-bed','sustain','cancellation'] for profile in ['identity','uniform','nonuniform']};observed=set();compared_operations=set()
    for comparison in comparisons:
        require(type(comparison) is dict)
        require(type(comparison.get('family')) is str and type(comparison.get('profile')) is str)
        pair=(comparison.get('family'),comparison.get('profile'));require(pair in expected and pair not in observed);observed.add(pair)
        operation=comparison.get('operation');require(identifier(operation) and operation in completed and operation not in compared_operations);compared_operations.add(operation)
        receipt=completed[operation]
        require(comparison.get('allPcmEqual') is True and digest(comparison.get('sourcePcmSha256')) and digest(comparison.get('prototypePcmSha256')) and comparison.get('workerPcmSha256')==comparison['prototypePcmSha256']==receipt['sampleSha256'])
        require(type(comparison.get('frames')) is int and comparison['frames']==receipt['target'] and type(comparison.get('channels')) is int and comparison['channels']==receipt['channels'])
    require(observed==expected)
    return {'protocol':PROTOCOL,'workerProcesses':33,'completedRenders':21,'fullPcmComparisons':18,'checks':q['checks'],'nativeAudio':False,'fullQualityQualified':False}
