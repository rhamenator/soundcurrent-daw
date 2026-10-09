#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optional Linux-only native authoring, with separately licensed vendor runtime.

Product and CI do not need or redistribute REAPER. Run inside a valid license /
evaluation period. A new private lab is required; frozen fixtures are untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from generate_media import generate

# Executed inside the PID/mount/network namespace. Fixed deadline, bounded logs,
# joined exact child, no display/audio socket or hardware device exposure.
SUPERVISOR = r'''
import json,pathlib,resource,subprocess,time
resource.setrlimit(resource.RLIMIT_FSIZE,(32*1024*1024,32*1024*1024))
p=subprocess.Popen(['/vendor/reaper','-cfgfile','/lab/profile/reaper.ini',
                    '-newinst','-nosplash','/lab/generate.lua'],
                   stdout=open('/lab/native-stdout.log','w'),
                   stderr=open('/lab/native-stderr.log','w'))
complete=False
start=time.monotonic()
try:
    while time.monotonic()-start < 20:
        if pathlib.Path('/lab/corpus/writer-complete.json').exists():
            complete=True;break
        if p.poll() is not None:break
        time.sleep(.05)
finally:
    exit_before_stop=p.poll()
    if p.poll() is None:
        p.terminate()
        try:p.wait(timeout=3)
        except subprocess.TimeoutExpired:p.kill();p.wait()
    receipt={'nativeNamespacePid':p.pid,'completeReceiptWritten':complete,
             'exitBeforeStop':exit_before_stop,'actualExitAfterStop':p.returncode,
             'seconds':time.monotonic()-start,'termination':'owned supervisor shutdown',
             'displayExposed':False,'hostAudioDevicesExposed':False,
             'networkNamespaceIsolated':True}
    pathlib.Path('/lab/authoring-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(0 if complete else 1)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vendor_directory", type=Path, help="Extracted REAPER/ directory")
    parser.add_argument("new_lab", type=Path, help="New empty lab directory")
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    vendor, lab = args.vendor_directory.resolve(), args.new_lab.resolve()
    manifest = json.loads((fixture / "manifest.json").read_text(encoding="utf-8"))
    for name, key in (("reaper", "executableSha256"), ("libSwell.so", "libSwellSha256"),
                      ("EULA.txt", "eulaSha256")):
        if hashlib.sha256((vendor / name).read_bytes()).hexdigest() != manifest["runtime"][key]:
            raise SystemExit("Vendor runtime differs from frozen native-writer receipt: " + name)
    if sys.platform != "linux" or not shutil.which("bwrap"):
        raise SystemExit("Authoring requires Linux and bubblewrap; CI only inspects frozen files")
    lab.mkdir(mode=0o700, exist_ok=False)
    (lab / "home").mkdir()
    (lab / "profile").mkdir()
    generate(lab / "corpus/media")
    for name in ("generate.lua", "blank.rpp"):
        shutil.copyfile(fixture / name, lab / name)
    (lab / "supervise.py").write_text(SUPERVISOR)
    args = ["bwrap", "--unshare-all", "--die-with-parent", "--new-session", "--clearenv",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/lib", "/lib",
            "--ro-bind", "/lib64", "/lib64", "--ro-bind", "/etc/fonts", "/etc/fonts",
            "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp", "--dir", "/run",
            "--ro-bind", str(vendor), "/vendor", "--bind", str(lab), "/lab",
            "--setenv", "PATH", "/usr/bin", "--setenv", "LANG", "C.UTF-8",
            "--setenv", "HOME", "/lab/home", "--setenv", "XDG_RUNTIME_DIR", "/run/user/1000",
            "--chdir", "/lab", "/usr/bin/python3", "/lab/supervise.py"]
    result = subprocess.run(args, timeout=30)
    if result.returncode:
        raise SystemExit("Isolated native authoring failed; owned lab retained")
    complete = json.loads((lab / "corpus/writer-complete.json").read_text())
    if complete["writer"] != manifest["writer"] or complete["cases"] != [c["id"] for c in manifest["cases"]]:
        raise SystemExit("Native writer emitted an incomplete/wrong corpus")
    for case in manifest["cases"]:
        witness = json.loads((lab / "corpus" / (case["id"] + ".observed.json")).read_text())
        if witness["beforeSave"] != witness["afterReopen"]:
            raise SystemExit("Native save/reopen changed its recorded properties")
        frozen = json.loads((fixture / case["observation"]).read_text(encoding="utf-8"))
        for document in (witness, frozen):
            for stage in ("beforeSave", "afterReopen"):
                for track in document[stage]["tracks"]:
                    track.pop("guid") # Newly authored identities legitimately differ.
        if witness != frozen:
            raise SystemExit("Regenerated properties differ from the frozen native API witness")
    print(json.dumps({"lab": str(lab), "writer": complete["writer"], "cases": len(complete["cases"]),
                      "frozenFixturesModified": False, "semanticImportQualified": False}))


if __name__ == "__main__":
    main()
