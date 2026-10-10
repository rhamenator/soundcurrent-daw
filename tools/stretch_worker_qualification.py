# SPDX-License-Identifier: GPL-3.0-only
"""Shared preview gate for a completed, independently verified native render."""
import re

PROTOCOL = 'sc-stretch-render-v4'
PROCESSOR = 'soundcurrent.stretch-rubberband4-r3-positioned-v2'


def qualify_stretch_worker(qualification, head, source_tree, executable_sha256, platform, *, protocol=PROTOCOL):
    def require(value):
        if not value:
            raise ValueError('Stretch helper lacks matching native process/artifact qualification')

    require(isinstance(qualification, dict))
    q = qualification
    formats = {'sc-stretch-render-v2':'sc-stretch-worker-qualification-v1',
               'sc-stretch-render-v3':'sc-stretch-worker-qualification-v2',
               PROTOCOL:'sc-stretch-worker-qualification-v3'}
    require(protocol in formats)
    expected_format = formats[protocol]
    require(q.get('format') == expected_format and
            q.get('sourceCommit') == head and q.get('sourceTree') == source_tree and
            q.get('nativePlatform') == platform and
            q.get('exeSha256') == executable_sha256 and
            re.fullmatch(r'[0-9a-f]{64}', executable_sha256 or '') is not None)
    require(q.get('protocol') == protocol and q.get('processor') == PROCESSOR and
            q.get('complete') is True and q.get('verifiedArtifact') is True and
            q.get('nativeAudio') is False)
    if protocol in ('sc-stretch-render-v3', PROTOCOL):
        require(type(q.get('unityShortCompletedJobs')) is int and q['unityShortCompletedJobs'] == 7 and
                q.get('unityIntegerCopyExact') is True and q.get('unity384kDiscreteCopyExact') is True)
    if protocol == PROTOCOL:
        require(type(q.get('explicitRegionCompletedJobs')) is int and q['explicitRegionCompletedJobs'] == 12 and
                q.get('regionCopyExact') is True and q.get('sameRoundedTargetDistinctKeys') is True and
                q.get('realContextRefusalBeforeMutation') is True)
    for key in ('pid', 'verifierPid', 'writtenFrames', 'checks', 'verifierChecks'):
        require(type(q.get(key)) is int and q[key] > 0)
    for key in ('exitCode', 'verifierExitCode'):
        require(type(q.get(key)) is int and q[key] == 0)
    for key in ('audioSha256', 'sampleSha256', 'verifierExeSha256', 'sourceTree'):
        pattern = r'[0-9a-f]{40}' if key == 'sourceTree' else r'[0-9a-f]{64}'
        require(type(q.get(key)) is str and re.fullmatch(pattern, q[key]) is not None)
    require(type(q.get('operation')) is str and
            re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', q['operation']) is not None)
    require(q['checks'] >= ({PROTOCOL:383, 'sc-stretch-render-v3':241, 'sc-stretch-render-v2':172}[protocol]) and q['verifierChecks'] >= 1045 and
            q['writtenFrames'] == 18000)
