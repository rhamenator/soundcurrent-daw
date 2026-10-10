#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded retained-file audit. Does not execute archived code or replay audio."""
from pathlib import Path
import array
import hashlib
import io
import json
import math
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "2026-10-10-native-restart-qualified"))
from capture_safety import check_member

sha = lambda b: hashlib.sha256(b).hexdigest()
summary = json.loads((ROOT / "summary.json").read_bytes())
packed = (ROOT / "capture.zip").read_bytes()
assert sha(packed) == summary["captureSha256"]
check_member("capture.zip", packed)
manifest = json.loads((ROOT / "capture-members.json").read_bytes())
with zipfile.ZipFile(io.BytesIO(packed)) as archive:
    assert len(archive.namelist()) == len(set(archive.namelist())) == 57
    assert set(archive.namelist()) == set(manifest)
    blobs = {n: archive.read(n) for n in archive.namelist()}
for name, b in blobs.items():
    assert manifest[name] == {"bytes": len(b), "sha256": sha(b)}
assert sha(blobs["operator/probe.cpp"]) == summary["probeSourceSha256"]
assert sha(blobs["operator/driver.py"]) == summary["driverSourceSha256"]
assert b"Invalid stretch project selection" in blobs["observations/initial-parent-error.log"]
assert b"Observation deadline expired" in blobs["observations/initial-relative-root-failure.log"]
assert b"100% tests passed, 0 tests failed out of 1" in blobs["observations/ctest.log"]
report = json.loads(blobs["report.json"])
assert report["platform"] == "Linux" and report["qualification"] == "production-controller-subprocess"
assert summary["cases"] == len(report["cases"]) == 4
assert report["probeSha256"] == "079f6c60eb0e7e80d514cd8efe728189e4ce67291a03c92cb90114dd05b3da2f"
assert report["helperSha256"] == "5ce259bb47f82400a2218e6d297b198aaa87bc65a58ca2e7aa02cedfb262fae7"


def pcm(b):
    assert b[:4] in (b"RIFF", b"RF64") and b[8:12] == b"WAVE"
    offset, fmt, data, d64 = 12, None, None, None
    while offset + 8 <= len(b):
        tag, size = struct.unpack_from("<4sI", b, offset)
        offset += 8
        assert offset + min(size, 28) <= len(b)
        if tag == b"ds64":
            assert size >= 28
            d64 = struct.unpack_from("<QQQ", b, offset)
        if tag == b"data" and size == 0xffffffff:
            assert d64
            size = d64[1]
        assert offset + size <= len(b)
        chunk = b[offset:offset + size]
        if tag == b"fmt ":
            assert fmt is None and size >= 16
            fmt = struct.unpack_from("<HHIIHH", chunk)
            assert fmt[0] == 3 or (fmt[0] == 65534 and size >= 28 and struct.unpack_from("<I", chunk, 24)[0] == 3)
        if tag == b"data":
            assert data is None
            data = chunk
        offset += size + (size & 1)
    assert offset == len(b) and fmt and data is not None
    assert (fmt[1], fmt[2], fmt[4], fmt[5]) == (2, 48000, 8, 32)
    assert fmt[3] == 48000 * 8 and len(data) == 1048576 * 8
    samples = array.array("f")
    samples.frombytes(data)
    assert samples.itemsize == 4
    if sys.byteorder != "little":
        samples.byteswap()
    assert all(math.isfinite(v) for v in samples) and max(map(abs, samples)) == 2
    return data, samples


expected = ["before-ack", "during-processing", "during-processing-helper-death", "after-complete"]
assert [c["stage"] for c in report["cases"]] == expected
parents = set()
for c in report["cases"]:
    stage, op = c["stage"], c["operation"]
    prefix = stage + "/project-été-Κиїв/"
    job = prefix + "media/derived/" + op + "/"
    parent = c["parentIdentity"]
    assert parent["pid"] not in parents and parent["startTicks"] > 0
    parents.add(parent["pid"])
    assert parent["exe"].endswith("/sc-stretch-parent-probe")
    assert c["parentExitCode"] == -9 and c["savedProjectAndRawUnchanged"]
    assert not c["installedGuiQualified"] and not c["windowsQualified"] and not c["powerLossQualified"]
    for n, h in c["fixtureAfter"].items():
        assert sha(blobs[prefix + n]) == h
    for n, h in c["fixtureBefore"].items():
        assert c["fixtureAfter"][n] == h
    assert c["terminalRecovery"]["projectSha256"] == c["fixtureBefore"]["project.json"]
    assert c["terminalRecovery"]["operation"] == op and not c["terminalRecovery"]["attached"]
    project = json.loads(blobs[prefix + "project.json"])
    assert project["tracks"][0]["clips"][0]["stretch"] is None
    selection = blobs[prefix + "media/stretch-selections/" + op + ".json"]
    selected = json.loads(selection)
    assert selected["operation"] == op and selected["format"] == "sc-stretch-selection-v1"
    assert (selected["request"]["timeNumerator"], selected["request"]["timeDenominator"], selected["request"]["pitchMilliCents"]) == (1, 1, 0)
    assert selected["policy"]["deadlineMilliseconds"] == 5000
    raw_pcm, raw_samples = pcm(blobs[prefix + "media/raw-été.wav"])
    if stage != "after-complete":
        helper = c["helperIdentity"]
        assert helper["parentPid"] == parent["pid"] and helper["startTicks"] > 0
        assert helper["exe"].endswith("/sc-stretch-render-worker")
        assert c["helperTerminal"]["pid"] == helper["pid"]
        assert c["liveRecovery"]["status"] == "Active" and not c["liveRecovery"]["reviewed"]
        assert c["liveRecovery"]["projectSha256"] == c["fixtureBefore"]["project.json"]
    complete = stage in ("during-processing", "after-complete")
    assert c["terminalRecovery"]["status"] == ("Ready" if complete else "Incomplete")
    assert c["terminalRecovery"]["reviewed"] == complete
    if complete:
        receipt = json.loads(blobs[job + "complete.json"])
        assert receipt["operation"] == op and receipt["complete"] and receipt["writtenFrames"] == 1048576
        assert receipt["audioSha256"] == sha(blobs[job + "audio.wav"])
        assert receipt["sourceSha256"] == sha(blobs[prefix + "media/raw-été.wav"])
        assert receipt["peakLinear"] == 2 and receipt["durabilityMinimum"] == "file-flushed"
        assert blobs[job + "selection.sha256"] == sha(selection).encode()
        assert blobs[job + "start.request"] == b"start"
        output_pcm, output_samples = pcm(blobs[job + "audio.wav"])
        assert output_samples == raw_samples  # Numeric unity-copy equality, not broad stretch quality.
        assert receipt["sampleSha256"] == sha(output_pcm)
        if stage == "during-processing":
            assert c["partialBytesAtParentDeath"] > 4096
            assert c["helperTerminal"]["exitCode"] == -13 and c["helperTerminal"]["waitStatus"] == 13
    else:
        assert job + "complete.json" not in blobs and job + "audio.wav" not in blobs
        if stage == "before-ack":
            assert c["helperTerminal"]["exitCode"] == 2 and c["helperTerminal"]["waitStatus"] == 512
            assert job + "start.request" not in blobs and job + "audio.partial" not in blobs
        else:
            assert c["helperTerminal"]["exitCode"] == -9 and c["helperTerminal"]["waitStatus"] == 9
            partial = blobs[job + "audio.partial"]
            assert len(partial) == c["partialBytesAtParentDeath"] > 4096
            assert sha(partial) == c["partialPreservedSha256"]
            assert blobs[job + "selection.sha256"] == sha(selection).encode()
assert summary["actualParentInterruptionQualified"] and summary["incompletePartialPreserved"]
assert summary["sourceAndProjectUnchanged"] and summary["noAutomaticAttachment"]
for key in ["installedGuiQualified", "windowsQualified", "audioEndpointActivated", "rubberBandQualityQualified",
            "powerLossQualified", "largeInventoryQualified", "fullGoalComplete", "productUploaded"]:
    assert summary[key] is False
print("Linux actual-parent interruption capture: four owned cases audited; no native/script/audio replay")
