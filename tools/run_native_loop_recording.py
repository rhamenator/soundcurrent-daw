#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Opt-in native loop-recording GUI fixture using a private instance of installed PipeWire.

Does not launch a session manager, connect physical endpoints or modify the user's
server. Retains logs, configuration and owned media, including on fixture failure.
Requires a locally built sc-pipewire-duplex-ui-fixture and PipeWire command-line tools.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
args = argparse.ArgumentParser(description=__doc__)
args.add_argument('--build-dir', type=Path, default=REPO / '.cache/build-desktop')
args.add_argument('--output-dir', type=Path, default=REPO / '.cache/native-loop-recording')
args.add_argument('--mode', choices=('loop', 'until-stop', 'normal'), default='loop')
args = args.parse_args()
args.output_dir.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='lr-', dir=args.output_dir.resolve()))
print(json.dumps({'ownedFolder': str(root)}), flush=True)
runtime = root / 'runtime'
runtime.mkdir(mode=0o700)
if len(os.fsencode(runtime / 'sc-loop-test-manager')) + 1 > 108:
    raise RuntimeError('Choose a shorter --output-dir for the private Unix socket')
installed = Path('/usr/share/pipewire/pipewire.conf').read_bytes()
config = installed.decode().replace('core.name   = pipewire-0', 'core.name   = sc-loop-test')
assert 'core.name   = sc-loop-test' in config, 'Installed configuration has an unfamiliar core name'
config = config.replace('context.properties = {', '''context.properties = {
    default.clock.rate = 48000
    default.clock.quantum = 1024
    default.clock.min-quantum = 1024
    default.clock.max-quantum = 1024
    module.x11.bell = false
    module.jackdbus-detect = false
    module.portal = false''', 1)
conf = root / 'owned.conf'
conf.write_text(config)
env = os.environ.copy()
env.update(PIPEWIRE_RUNTIME_DIR=str(runtime), PIPEWIRE_REMOTE='sc-loop-test',
           QT_QPA_PLATFORM='offscreen')
exe = (args.build_dir / 'sc-pipewire-duplex-ui-fixture').resolve(strict=True)
invocation = {'mode': args.mode, 'ownedRuntime': str(runtime), 'privateDaemon': True,
              'fixtureSha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'installedConfigSha256': hashlib.sha256(installed).hexdigest(),
              'configSha256': hashlib.sha256(conf.read_bytes()).hexdigest(),
              'vmUsed': False, 'physicalOutputSelected': False}
log = open(root / 'daemon.txt', 'wb')
server = subprocess.Popen(['pipewire', '-c', str(conf)], env=env, stdout=log, stderr=subprocess.STDOUT)
try:
    deadline = time.monotonic() + 8
    while not (runtime / 'sc-loop-test').exists():
        assert server.poll() is None, 'Owned daemon exited'
        if time.monotonic() > deadline:
            raise RuntimeError('Owned socket timeout')
        time.sleep(.05)
    def inventory(label):
        objects = json.loads(subprocess.check_output(['pw-dump'], env=env, timeout=5))
        (root / (label + '.json')).write_text(json.dumps(objects, indent=2) + '\n')
        nodes = [r['info']['props'].get('node.name') for r in objects
                 if r['type'] == 'PipeWire:Interface:Node']
        invocation[label + 'Nodes'] = nodes
        assert set(nodes) == {'Dummy-Driver', 'Freewheel-Driver'}, nodes
    inventory('before')
    command = [str(exe), str(root / 'project'), '3']
    if args.mode != 'normal':
        command.append(args.mode)
    invocation['command'] = command
    with open(root / 'stdout.json', 'wb') as stdout, open(root / 'stderr.txt', 'wb') as stderr:
        run = subprocess.run(command, env=env,
                             stdout=stdout, stderr=stderr, timeout=45)
    invocation['exitCode'] = run.returncode
    inventory('after')
    if run.returncode:
        raise RuntimeError('Native GUI fixture failed; inspect retained stderr')
finally:
    server.terminate()
    try:
        server.wait(timeout=5)
    except subprocess.TimeoutExpired:
        server.kill()
        server.wait()
    log.close()
    invocation['daemonRetired'] = server.poll() is not None
    (root / 'invocation.json').write_text(json.dumps(invocation, indent=2) + '\n')
    (args.output_dir / 'latest-path.txt').write_text(str(root) + '\n')
print(json.dumps({'folder': str(root), 'exitCode': invocation['exitCode'],
                  'daemonRetired': invocation['daemonRetired']}))
