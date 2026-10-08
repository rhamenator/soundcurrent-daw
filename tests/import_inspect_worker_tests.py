#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real separate worker processes, original synthetic input; no native DAW claim."""
import hashlib
import json
import os
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

worker = Path(sys.argv[1]).resolve()
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(message)


def invoke(path, *options):
    process = subprocess.Popen([str(worker), '--rpp', str(path), *map(str, options)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = process.communicate(timeout=15)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        raise AssertionError('Worker exceeded acceptance deadline')
    return process.pid, process.returncode, stdout, stderr


def reject(path, message_id, *options):
    _, code, stdout, stderr = invoke(path, *options)
    check(code == 1, f'Wrong worker refusal exit: {code}: {stderr[:200]!r}')
    check(not stdout, 'Refusal published a partial/success inventory')
    error = json.loads(stderr)
    check(error == {'protocol': 'sc-import-inspection-v1', 'complete': False,
                    'messageId': message_id}, 'Unstable or foreign-text error diagnostic')


with tempfile.TemporaryDirectory(prefix='sc-import-worker-') as temporary:
    root = Path(temporary)
    # Non-ASCII directory and basename on both platforms. Arbitrary foreign byte
    # payload must not be decoded or put into JSON as a property value.
    directory = root/'été-Ελληνικά-Київ'
    directory.mkdir()
    path = directory/'příliš-Ångström.rpp'
    source = (b'\xef\xbb\xbf\r\n<REAPER_PROJECT 0.1 7.74 1\r\n'
              b' <TRACK foreign-identity\r\n'
              b'  NAME "a name"\r\n'
              b'  FILE "../unapproved-media.wav"\r\n'
              b'  SCRIPT `opaque-program-not-run`\r\n'
              b'  <UNKNOWN_PLUGIN vendor\r\n'
              b'   opaque-\xff-\xfe-01234+/=\r\n'
              b'  >\r\n >\r\n>\r\n')
    path.write_bytes(source)
    before = hashlib.sha256(source).hexdigest()
    pid, code, stdout, stderr = invoke(path)
    check(code == 0 and not stderr, f'Inspection failed: {code}: {stderr!r}')
    report = json.loads(stdout)
    check(report['protocol'] == 'sc-import-inspection-v1' and report['complete'] is True,
          'Missing complete versioned protocol')
    check(report['workerPid'] == pid and pid != os.getpid(), 'Parser ran in the parent process')
    check(report['adapter'] == 'rpp-outline-v1' and report['sourceFormat'] == 'reaper-rpp',
          'Unversioned adapter identity')
    check(report['source'] == {'bytes': len(source), 'sha256': before,
                              'storage': 'worker-memory-snapshot',
                              'consistency': 'size-and-mtime-checked-not-atomic'},
          'Captured bytes/provenance mismatch or atomic-file claim')
    check(report['nativeCompatibility'] == 'unqualified' and report['semanticStatus'] == 'unverified',
          'Structural parsing promoted native compatibility')
    check(report['writerVersion']['status'] == 'unverified', 'Writer version inferred from a raw token')
    check(report['writerVersion']['headerRange'] == report['nodes'][report['root']]['lineRange'],
          'Header token identity lost')
    position = 0
    for i, node in enumerate(report['nodes']):
        begin, size = node['lineRange']
        check(node['index'] == i and begin == position and size > 0, 'Line gap/overlap/index mismatch')
        position += size
        check(node['status'] == 'unverified' and node['originalBytesRetained'] is True,
              'Unknown state silently converted/discarded')
        for name in ('lineRange', 'keyRange', 'extentRange'):
            begin, size = node[name]
            check(begin >= 0 and size >= 0 and begin+size <= len(source), 'Unsafe original-source range')
        if node['parent'] is not None:
            parent = report['nodes'][node['parent']]
            check(node['parent'] < i and parent['kind'] == 'block-open', 'Invalid parent identity')
            begin, size = parent['extentRange']
            check(begin <= node['lineRange'][0] and position <= begin+size, 'Child escaped parent')
    check(position == len(source), 'Foreign source bytes missing from report inventory')
    check(b'opaque-program' not in stdout and b'unapproved-media' not in stdout,
          'Opaque foreign content interpolated into protocol')
    check(path.read_bytes() == source and list(directory.iterdir()) == [path],
          'Worker changed the input or created media/project/script artifacts')

    chunks = root/'chunk-boundaries.rpp'
    chunk_source = b'<REAPER_PROJECT\n'+(b'OPAQUE '+b'x'*65000+b'\n')*2+b'>\n'
    chunks.write_bytes(chunk_source)
    _, code, chunk_report, err = invoke(chunks)
    check(code == 0 and not err and
          json.loads(chunk_report)['source']['sha256'] == hashlib.sha256(chunk_source).hexdigest(),
          'Multi-chunk load/hash changed source bytes')
    check(len(chunk_report) < 4096 and chunks.read_bytes() == chunk_source,
          'Long opaque line expanded report storage or changed source')

    reject(path, 'import.resource_limit', '--memory-bytes', 1)
    reject(path, 'import.resource_limit', '--maximum-input-bytes', len(source)-1)
    # Process IDs can differ in decimal width between launches (especially on
    # Windows); refuse below every possible PID-width variation.
    reject(path, 'import.resource_limit', '--maximum-report-bytes', len(stdout)-16)
    _, code, same, err = invoke(path, '--maximum-input-bytes', len(source),
                               '--maximum-report-bytes', len(stdout)+20)
    check(code == 0 and not err and json.loads(same)['source']['sha256'] == before,
          'Admitted input/report boundary changed original content')
    reject(path, 'import.invalid_request', '--maximum-report-bytes', 0)
    reject(path, 'import.invalid_request', '--unknown-limit', 42)
    reject(root/'missing.rpp', 'import.io_error')
    reject(directory, 'import.io_error')
    broken = root/'broken.rpp'
    for content in (b'', b'<REAPER_PROJECT\n', b'<REAPER_PROJECT\n>\n>\n',
                    b'<REAPER_PROJECT\nNUL\0\n>\n'):
        broken.write_bytes(content)
        reject(broken, 'import.invalid_structure')
        check(broken.read_bytes() == content, 'Rejected input modified')
    if os.name != 'nt':
        linked = root/'linked.rpp'
        linked.symlink_to(path)
        reject(linked, 'import.io_error')
        fifo = root/'pipe.rpp'
        os.mkfifo(fifo)
        reject(fifo, 'import.io_error')
        reject(Path('/dev/zero'), 'import.io_error')
        process = subprocess.Popen([str(worker), '--rpp', str(path)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        process.stdout.close()
        try:
            process.wait(timeout=15)
            check(process.returncode == 1 and
                  json.loads(process.stderr.read())['messageId'] == 'import.io_error',
                  'Closed parent report pipe was not a checked I/O refusal')
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)

    # Bounded report pipe backpressure keeps the child live after the first
    # byte. Cancel this exact process and drain it; never restart on a timeout.
    large = root/'cancel.rpp'
    large_source = b'<REAPER_PROJECT\n'+b'OPAQUE preserved\n'*35000+b'>\n'
    large.write_bytes(large_source)
    process = subprocess.Popen([str(worker), '--rpp', str(large)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        # An external first-byte deadline also retires this exact child if it
        # stalls before producing a report. The reader is joined before drain.
        with ThreadPoolExecutor(max_workers=1) as reader:
            pending = reader.submit(process.stdout.read, 1)
            try:
                first = pending.result(timeout=15)
            except FutureTimeout:
                process.kill()
                pending.result(timeout=10)
                process.communicate(timeout=10)
                raise AssertionError('Live-cancel worker missed first-byte deadline')
        check(first == b'{' and process.poll() is None, 'Cancellation target was not a live worker')
        if os.name == 'nt':
            process.terminate()  # Actual Windows termination, no cooperative-exit claim.
        else:
            process.send_signal(signal.SIGTERM)
        stdout, stderr = process.communicate(timeout=10)
        check(process.returncode != 0, 'Canceled worker published a success exit')
        if os.name != 'nt':
            check(process.returncode == 3 and json.loads(stderr)['messageId'] == 'import.canceled',
                  'Signal did not reach cooperative parser/writer cancellation')
        try:
            decoded = json.loads(first+stdout)
        except (ValueError, UnicodeError):
            decoded = None
        check(decoded is None or decoded.get('complete') is not True,
              'Canceled output could be accepted as a complete report')
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=10)
    check(large.read_bytes() == large_source and path.read_bytes() == source,
          'Canceled process altered original media/project input')
    check(set(root.iterdir()) == {directory, broken, large, chunks} | ({linked, fifo} if os.name != 'nt' else set()),
          'Worker wrote unapproved destination files')

print(json.dumps({'checks': checks, 'processBoundary': True, 'sourceUntouched': True,
                  'nativeDAWCompatibilityQualified': False, 'platform': sys.platform}))
