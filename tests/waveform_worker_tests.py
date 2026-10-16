# SPDX-License-Identifier: GPL-3.0-only
"""Real isolated worker, independently generated WAVE and wire/peak oracle.

No physical audio or project changes. Each child has a fixed original 10s deadline;
the retained temporary fixture is printed on failure and removed only on success.
"""
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

root = Path(tempfile.mkdtemp(prefix="sc-waveform-worker-"))
worker = str(Path(sys.argv[1]).resolve())
operation = "12345678-1234-4234-8234-123456789abc"
reports = []


def wave(channels, pcm):
    data = struct.pack("<" + "d" * len(pcm), *pcm)
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 3, channels, 48000, 48000 * channels * 8,
                        channels * 8, 64) + b"data" + struct.pack("<I", len(data)) + data)


def run_case(channels, frames, width, change=None, pcm_override=None, window=(0, 2**64-1)):
    pcm = ([((f * channels + c) % 29 - 12) / 2 for f in range(frames) for c in range(channels)]
           if pcm_override is None else pcm_override)
    if pcm and pcm_override is None:
        pcm[-1] = math.nextafter(0.25, 1)
    data = wave(channels, pcm)
    path = root / "audio-été.wav"
    path.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    options = {"root": str(root), "relative": path.name, "sha256": digest, "frames": str(frames),
               "rate": "48000", "channels": str(channels), "base-frames": str(width),
               "operation": operation, "epoch": "9", "generation": "47",
               "maximum-bytes": str(len(data)), "memory-bytes": str(128 * 1024 * 1024),
               "window-first": str(window[0]), "window-frames": str(window[1])}
    if change:
        options.update(change)
    args = [worker] + [item for key, value in options.items() for item in ("--" + key, value)]
    child = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        out, err = child.communicate(timeout=10)
    except BaseException:
        child.kill()
        child.communicate()
        raise
    reports.append({"channels": channels, "frames": frames, "base": width,
                    "change": change, "pid": child.pid, "exit": child.returncode,
                    "stdoutBytes": len(out), "stderr": err.decode("utf-8", errors="replace")})
    assert path.read_bytes() == data, "Worker mutated source"
    assert set(p.name for p in root.iterdir()) == {path.name}, "Worker created a file"
    if change or pcm_override is not None:
        assert child.returncode != 0 and not out, "Failed request published waveform"
        assert err.startswith(b"sc-waveform-v2:"), err
        return
    assert child.returncode == 0 and err == b"", (child.returncode, err)
    header = (b"SCWAVE02" + operation.encode() + struct.pack("<QQQ", 9, 47, child.pid) +
              digest.encode() + struct.pack("<QIIQ", frames, 48000, channels, width) + struct.pack("<QQ", *window))
    assert len(header) == 172 and out[:172] == header, "Wrong protocol identity or byte order"
    expected = bytearray(header)
    start = window[0]
    count = frames if window[1] == 2**64-1 else window[1]
    while count:
        bins = (count + width - 1) // width
        for b in range(bins):
            for c in range(channels):
                values = [pcm[f * channels + c] for f in range(start + b * width, start + min(count, (b + 1) * width))]
                expected.extend(struct.pack("<dd", min(values), max(values)))
        if bins == 1:
            break
        width = min(count, width * 2)
    assert out == expected, "Independent source extrema or exact wire extent differs"


try:
    for channels in (1, 2, 257, 1024):
        for frames in (0, 1, 19):
            for width in (1, 3, 64):
                run_case(channels, frames, width)
    for channels in (1, 2, 257, 1024):
        for window in ((5, 9), (7, 1), (18, 1), (19, 0), (0, 0)):
            for width in (1, 3, 64):
                run_case(channels, 19, width, window=window)
    for change in ({"window-first": "20", "window-frames": "0"},
                   {"window-first": "18", "window-frames": "2"},
                   {"window-first": "1"},{"sha256": "0" * 64}, {"frames": "20"}, {"rate": "96000"},
                   {"channels": "1"}, {"base-frames": "0"}, {"epoch": "-1"},
                   {"frames": str(2**64)}, {"memory-bytes": "1"},
                   {"relative": "../audio-été.wav"}, {"relative": "missing.wav"},
                   {"root": "."}, {"maximum-bytes": "1"}, {"operation": "bad"}):
        run_case(2, 19, 3, change)
    for invalid in (math.inf, -math.inf, math.nan):
        run_case(2, 19, 3, pcm_override=[0.25] * 37 + [invalid])
    run_case(2, 19, 1, pcm_override=[0.25] * 37 + [math.nan], window=(1, 5))
    run_case(1024, 19, 1, {"memory-bytes": str(1024 * 1024)})
    print(json.dumps({"passed": True, "cases": len(reports), "reports": reports}, indent=2))
    shutil.rmtree(root)
except BaseException:
    (root / "failed-cases.json").write_text(json.dumps(reports, indent=2))
    print("Retained failed waveform worker fixture: " + str(root), file=sys.stderr)
    raise
