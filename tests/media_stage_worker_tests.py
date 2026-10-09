# SPDX-License-Identifier: GPL-3.0-only
"""Actual child, opaque byte preservation; no audio route or project mutation."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid

worker = str(Path(sys.argv[1]).resolve())
wave_probe = str(Path(sys.argv[2]).resolve()) if len(sys.argv) == 3 else None
checks = 0

def check(condition, message):
    global checks
    checks += 1
    assert condition, message

with tempfile.TemporaryDirectory(prefix="sc-stage-été-Κиїв-") as directory:
    fixture = Path(directory)
    source = fixture / "source"
    destination = fixture / "destination"
    source.mkdir()
    destination.mkdir()
    # Independent IEEE float WAVE with preserved headroom, Unicode source leaf.
    samples = [0.0, -2.5, 1.75, 0.125] * 20000
    data = struct.pack("<" + "f" * len(samples), *samples)
    fmt = struct.pack("<HHIIHH", 3, 2, 48000, 384000, 8, 32)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    wave = b"RIFF" + struct.pack("<I", len(body)) + body
    leaf = "été-Κиїв.wav"
    (source / leaf).write_bytes(wave)
    digest = hashlib.sha256(wave).hexdigest()

    def invoke(relative=leaf, sha=digest, size=len(wave), target=destination, operation=None):
        operation = operation or str(uuid.uuid4())
        args = [worker, "--root", str(source), "--relative", relative,
                "--destination", str(target), "--bytes", str(size), "--sha256", sha,
                "--operation", operation]
        with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            pid = child.pid
            stdout, stderr = child.communicate(timeout=15)
            check(len(stdout) <= 8192 and len(stderr) <= 1024, "Worker banks exceeded")
            return child.returncode, stdout, stderr, pid, operation

    code, stdout, stderr, pid, operation = invoke()
    report = json.loads(stdout)
    check(code == 0 and not stderr, "Actual copy worker refused valid owned source")
    check(set(report) == {"protocol", "verified", "publishedAsset", "operation", "relative", "bytes", "sha256", "durability", "workerPid", "decodedAudio"}, "Wrong worker schema")
    check(report["protocol"] == "sc-media-stage-v1" and report["verified"] is True, "Wrong verification status")
    check(report["publishedAsset"] is False and report["decodedAudio"] is False, "Opaque staging claimed conversion or decoding")
    check(type(report["workerPid"]) is int and report["workerPid"] == pid, "Report is not actual child")
    check(report["operation"] == operation and report["relative"] == operation + "/media.partial", "Foreign reference became destination path")
    check(report["bytes"] == len(wave) and report["sha256"] == digest, "Wrong staged snapshot")
    check(report["durability"] == (1 if os.name == "nt" else 2), "Platform durability overstated")
    if wave_probe:
        decoded = []
        for root_path, reference in [(source, leaf), (destination, report["relative"])]:
            child = subprocess.run([wave_probe, "--root", str(root_path), "--relative", reference,
                                    "--maximum-bytes", str(len(wave))], capture_output=True, timeout=15)
            check(child.returncode == 0 and not child.stderr, "Actual decoder refused staged float WAVE")
            audio = json.loads(child.stdout)
            check(audio["sourceSha256"] == digest and audio["peakLinear"] == 2.5 and audio["frames"] == len(samples) // 2,
                  "WAVE bytes, headroom or frame count changed")
            decoded.append({key: value for key, value in audio.items() if key != "relative"})
        check(decoded[0] == decoded[1], "Source and destination decode reports differ")
    copied = (destination / operation / "media.partial").read_bytes()
    check(copied == wave and hashlib.sha256(copied).hexdigest() == digest, "Independent float payload was not byte identical")
    check(struct.unpack_from("<f", copied, 48)[0] == -2.5, "Floating headroom changed")
    check((source / leaf).read_bytes() == wave, "Source changed")
    check(not (destination / "project.json").exists(), "Worker published current project")

    sentinel = destination / operation / "sentinel"
    sentinel.write_bytes(b"preserve")
    for overrides, expected in [({"operation": operation}, 4), ({"sha": "0" * 64}, 6),
                                ({"size": len(wave) - 1}, 6), ({"relative": "missing.wav"}, 5),
                                ({"relative": "../source/" + leaf}, 3), ({"target": Path("relative")}, 3),
                                ({"sha": "G" * 64}, 3), ({"target": destination / "absent"}, 4)]:
        before = sorted(p.name for p in destination.iterdir())
        code, stdout, stderr, _, _ = invoke(**overrides)
        error = json.loads(stderr)
        check(code != 0 and not stdout and error["verified"] is False and error["publishedAsset"] is False, "Failure published success")
        check(error["errorCode"] == expected and error["messageId"] == "import.media_stage_failed", "Wrong stable refusal")
        check(sorted(p.name for p in destination.iterdir()) == before, "Preflight refusal created destination")
        check(sentinel.read_bytes() == b"preserve" and (source / leaf).read_bytes() == wave, "Refusal modified existing data")

    # Source changed after an earlier check; same length, different checksum.
    changed = bytearray(wave)
    changed[-1] ^= 1
    (source / leaf).write_bytes(changed)
    code, stdout, stderr, _, operation = invoke()
    check(code != 0 and not stdout and json.loads(stderr)["errorCode"] == 6, "Stale checked hash was accepted")
    check(not (destination / operation).exists(), "Stale check mutated destination")

print(f"Passed {checks} actual media staging worker checks")
