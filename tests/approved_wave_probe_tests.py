# SPDX-License-Identifier: GPL-3.0-only
"""Real child/argv/WAVE/hash gate; independently authored original byte fixtures."""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

checks = 0
tool = Path(sys.argv[1]).resolve()


def check(condition):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(f"WAVE probe check {checks} failed")


def wave(data, bits=32, channels=2, floating=True):
    alignment = channels * (bits // 8)
    fmt = struct.pack("<HHIIHH", 3 if floating else 1, channels, 48000,
                      48000 * alignment, alignment, bits)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    if len(data) % 2:
        body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


with tempfile.TemporaryDirectory(prefix="sc-wave-Κиїв-") as tmp:
    root = Path(tmp)
    relative = "été-Κиїв-<html>.wav"  # A nonportable token needs explicit mapping.
    path = root / "été-Κиїв.wav"
    data = wave(struct.pack("<6f", 2.5, -3.25, 0, 1, -0.5, 0.25))
    path.write_bytes(data)

    def run(ref=path.name, maximum=str(len(data))):
        return subprocess.run([str(tool), "--root", str(root), "--relative", ref,
                               "--maximum-bytes", maximum], capture_output=True, timeout=10)

    def failed(result):
        check(result.returncode != 0 and result.stdout == b"")
        report = json.loads(result.stderr)
        check(report["protocol"] == "sc-approved-wave-validation-v1" and not report["complete"])
        check(report["messageId"] == "import.wave_validation_failed" and len(result.stderr) < 1024)

    result = run()
    check(result.returncode == 0 and result.stderr == b"")
    check(result.stdout.isascii() and len(result.stdout) < 2048)
    report = json.loads(result.stdout)
    check(report["protocol"] == "sc-approved-wave-validation-v1" and report["complete"])
    check(report["sourceBytes"] == len(data) and report["sourceSha256"] == hashlib.sha256(data).hexdigest())
    check(report["frames"] == report["decodedFrames"] == 3 and report["channels"] == 2 and report["rateHz"] == 48000)
    check(report["peakLinear"] == 3.25 and report["bitsPerSample"] == 32 and report["encodingId"] == 5)
    check(report["containerId"] == "riff" and not report["extensible"] and report["channelMask"] == 0)
    check(report["relative"] == path.name and report["bytesRead"] >= 2 * len(data) and report["ioOperations"] > 0)
    check(path.read_bytes() == data and str(root) not in result.stdout.decode("ascii"))
    # Exact replacement mode is a distinct protocol; no filename fallback.
    match_args = [str(tool), "--match-asset", "--root", str(root), "--relative", path.name,
                  "--maximum-bytes", str(len(data)), "--asset-sha256", hashlib.sha256(data).hexdigest(),
                  "--asset-frames", "3", "--asset-rate", "48000", "--asset-channels", "2"]
    exact = subprocess.run(match_args, capture_output=True, timeout=10)
    check(exact.returncode == 0 and exact.stderr == b"")
    matched = json.loads(exact.stdout)
    check(matched["protocol"] == "sc-asset-replacement-check-v1" and matched["matchesAsset"] and matched["complete"])
    check(matched["sourceSha256"] == hashlib.sha256(data).hexdigest() and matched["peakLinear"] == 3.25)
    for index, value in ((9, "0" * 64), (9, "z" * 64), (11, "4"), (13, "44100"),
                         (15, "1"), (11, "-1"), (13, str(2**32)), (15, "2junk"), (5, "missing.wav")):
        wrong = match_args.copy(); wrong[index] = value
        refused = subprocess.run(wrong, capture_output=True, timeout=10)
        check(refused.returncode != 0 and refused.stdout == b"")
        failure = json.loads(refused.stderr)
        check(failure["protocol"] == "sc-asset-replacement-check-v1" and not failure["complete"])
        if (index, value) in ((9, "0" * 64), (11, "4"), (13, "44100"), (15, "1")):
            check(failure["errorCode"] == 6)  # MediaMismatch, not argv rejection.
        check(len(refused.stderr) < 1024 and str(root) not in refused.stderr.decode())
    check(path.read_bytes() == data)
    for maximum in ("0", "-1", "1", str(2**64), "64junk"):
        failed(run(maximum=maximum))
    for ref in ("../outside.wav", "missing.wav", relative, "C:/audio.wav", "CON.wav"):
        failed(run(ref=ref))
    for corrupt in (data[:-1], data[:40] + struct.pack("<I", 10**6) + data[44:],
                    wave(struct.pack("<f", math.nan), channels=1), b"RF64" + data[4:], b"not a wave file"):
        (root / "bad.wav").write_bytes(corrupt)
        failed(run("bad.wav", "1048576"))
    # Existing frozen Linux-authored media: independent header and byte checksum.
    corpus = Path(__file__).resolve().parent / "fixtures/reaper-7.82/projects"
    for name, channels in (("mono.wav", 1), ("stereo.wav", 2)):
        source = corpus / "media" / name
        original = source.read_bytes()
        result = subprocess.run([str(tool), "--root", str(corpus), "--relative", "media/" + name,
                                 "--maximum-bytes", "1048576"], capture_output=True, timeout=10)
        check(result.returncode == 0 and result.stderr == b"")
        report = json.loads(result.stdout)
        check(report["sourceSha256"] == hashlib.sha256(original).hexdigest() and report["sourceBytes"] == len(original))
        check(report["frames"] == report["decodedFrames"] == 96000 and report["channels"] == channels and report["rateHz"] == 48000)
        # Original authored int16 formula spans -2046..2046, exactly normalized.
        check(report["peakLinear"] == 2046 / 32768 and report["encodingId"] == 2)
        check(source.read_bytes() == original)

print(json.dumps({"checks": checks, "platform": sys.platform, "realWorker": True,
                  "independentByteHashAndFloatHeadroom": True, "originalCorpusFiles": 2,
                  "sourceUntouched": True, "projectConversion": False, "alignedRenderComparison": False}))
