#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained installed Windows observations; no native or archived-code replay."""
from pathlib import Path
import copy
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

sha = lambda data: hashlib.sha256(data).hexdigest()
summary = json.loads((ROOT / "summary.json").read_bytes())
manifest = json.loads((ROOT / "capture-members.json").read_bytes())
archive = (ROOT / "capture.zip").read_bytes()
assert sha(archive) == summary["captureSha256"]
check_member("capture.zip", archive)
with zipfile.ZipFile(io.BytesIO(archive)) as outer:
    assert len(outer.namelist()) == len(set(outer.namelist())) == 18
    assert set(outer.namelist()) == set(manifest)
    blobs = {name: outer.read(name) for name in outer.namelist()}
for name, data in blobs.items():
    assert manifest[name] == {"bytes": len(data), "sha256": sha(data)}, name

def captured(name, suffix):
    with zipfile.ZipFile(io.BytesIO(blobs["captures/" + name])) as archive:
        names = [n for n in archive.namelist() if not n.endswith(("/", "\\")) and
                 n.replace("\\", "/").endswith(suffix)]
        assert len(names) == 1, (name, suffix, names)
        return archive.read(names[0])

FINAL = "restart-complete-20261010202950277.zip"
def data(suffix):
    return captured(FINAL, suffix)

def load(suffix):
    return json.loads(data(suffix).decode("utf-8-sig"))

def events(stamp):
    return {row["name"]: row["data"] for row in load("records/restart-" + stamp + ".json")}

def tree(stamp, stage):
    return load("records/uia-restart-" + stamp + "-" + stage + ".json")

def control(rows, suffix):
    values = [r for r in rows if r["id"].endswith("." + suffix) and r["class"]]
    assert len(values) == 1, suffix
    return values[0]

def wave(b):
    # Same RIFF/RF64 float32 contract as the installed Linux recovery verifier.
    assert b[:4] in (b"RIFF", b"RF64") and b[8:12] == b"WAVE"
    offset, fmt, pcm, d64 = 12, None, None, None
    while offset + 8 <= len(b):
        tag, size = struct.unpack_from("<4sI", b, offset)
        offset += 8
        if tag == b"ds64":
            assert size >= 28
            d64 = struct.unpack_from("<QQQ", b, offset)
        if tag == b"data" and size == 0xffffffff:
            assert d64
            size = d64[1]
        assert offset + size <= len(b)
        chunk = b[offset:offset + size]
        if tag == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", chunk)
            assert fmt[0] == 3 or (fmt[0] == 65534 and struct.unpack_from("<I", chunk, 24)[0] == 3)
        if tag == b"data":
            assert pcm is None
            pcm = chunk
        offset += size + (size & 1)
    assert offset == len(b) and fmt and pcm is not None
    assert fmt[5] == 32 and fmt[4] == fmt[1] * 4 and fmt[3] == fmt[2] * fmt[4]
    assert len(pcm) % fmt[4] == 0
    samples = struct.unpack("<" + "f" * (len(pcm) // 4), pcm)
    assert all(math.isfinite(value) for value in samples)
    return {"channels": fmt[1], "rate": fmt[2], "frames": len(pcm) // fmt[4],
            "peak": max(abs(value) for value in samples)}, pcm, samples

HEAD = "4fb8c1cdf1b1365e5834256f4b39b51566dbed52"
OP = "eee85921-5fd6-497f-88be-ecfd1a260bca"
RAW = "8bdbf77d-a3bf-4d55-987b-a161683b0105"
MAIN_SHA = "2302a5d5d2da63476a2f1f7074053b64d75a379d31973916bc35881ae29d57bd"
WORKER_SHA = "ca352ca8325219b0dce6a61029878a76f85fc2cdd8d42d75f102b4ba248a2731"
PROJECT = "project-été-Κиїв/"
DERIVED = PROJECT + "media/derived/" + OP + "/"
assert summary["sourceHead"] == HEAD
assert summary["sourceTree"] == "d1b6be0bd9114af21d73a5dd6ba8172b53510c44"
assert summary["installedSequence"] == "20261010183111"
assert summary["operation"] == OP
main = load("records/main-20261010195312642.json")
assert main["pid"] == 3376 and main["sourceHead"] == HEAD and main["exeSha256"] == MAIN_SHA
assert not main["developerSdkUsed"] and not main["nativeAudioActivated"]
first = events("20261010202152514")
processing = events("20261010202504192")
history = events("20261010202735184")
attached = events("20261010202950277")
for event, pid in [(first["launch-recover"], 6568), (history["launch-attached"], 3644)]:
    assert event["pid"] == pid and event["sha256"] == MAIN_SHA and event["sessionId"] != 0
    assert not event["developerSdkUsed"] and not event["nativeAudioActivated"]
    install = event["exe"].rsplit("\\", 1)[0].casefold() + "\\"
    for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "sndfile.dll", "qwindows.dll"]:
        found = [m for m in event["modules"] if m["name"] == name]
        assert len(found) == 1 and found[0]["path"].casefold().startswith(install), name
install = main["exe"].rsplit("\\", 1)[0].casefold() + "\\"
for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "sndfile.dll", "qwindows.dll"]:
    found = [m for m in main["modules"] if m["name"] == name]
    assert len(found) == 1 and found[0]["path"].casefold().startswith(install), name
close = load("records/action-normal-close.json")
assert close["pid"] == 3376 and close["exited"] and close["normalCloseRequested"]
assert close["exitCode"] == 0 and close["forced"] is False
for event, pid in [(history["quit-recover"], 6568), (attached["quit-attached"], 3644)]:
    assert event["pid"] == pid and event["actualExited"] and event["normalCloseRequested"]
    assert event["exitCode"] == 0 and event["forced"] is False
assert main["pid"] != first["launch-recover"]["pid"] != history["launch-attached"]["pid"]
assert close["utc"] < first["launch-recover"]["startUtc"] < history["launch-attached"]["startUtc"]
observer = load("records/helper-observer.json")
assert observer["stopped"] and observer["traceRegistered"] is False
assert len(observer["processes"]) == 1
helper = observer["processes"][0]
assert helper["pid"] == 6584 and helper["sha256"] == WORKER_SHA
assert helper["path"].rsplit("\\", 1)[0].casefold() == main["exe"].rsplit("\\", 1)[0].casefold()

render_capture = "action-4fb8-second-quit-200942092.zip"
initial_bytes = captured(render_capture, PROJECT + "project.json")
assert data("records/project-restart-20261010202152514-opened-recover.json") == initial_bytes
assert data("records/project-restart-20261010202504192-ready-reviewed.json") == initial_bytes
initial = json.loads(initial_bytes)
applied = load("records/project-restart-20261010202504192-applied.json")
undone = load("records/project-restart-20261010202735184-undo-saved.json")
redone = load("records/project-restart-20261010202735184-redo-saved.json")
assert initial["schemaMinor"] == 12 and undone["schemaMinor"] == 15
canonical_initial = copy.deepcopy(initial)
canonical_initial["schemaMinor"] = 15
assert undone == canonical_initial  # Sole persisted normalization is schema 12 -> 15.
assert redone == applied == load(PROJECT + "project.json")
assert history["undo-saved"]["projectSha256"] != history["redo-saved"]["projectSha256"]
assert applied["tracks"][0]["clips"][0]["assetId"] == OP
assert applied["tracks"][0]["clips"][0]["lengthFrames"] == 49152
assert initial["tracks"][0]["clips"][0]["assetId"] == RAW
anchor = applied["tracks"][0]["clips"][0]["stretch"]
assert anchor["sourceAssetId"] == RAW and anchor["sourceFrames"] == 32768
assert anchor["settings"] == {"timeNumerator": 3, "timeDenominator": 2, "pitchMilliCents": 0, "formantPreserved": True}
assert anchor["warp"]["markers"] == [{"id": "9af9fd13-349c-4417-abd6-941f0cea1652", "source": [4096, 0, 1], "output": [6144, 0, 1]}]
assert processing["inventory-recover"]["reviewEnabled"]
assert "Ready for review" in processing["inventory-recover"]["name"]
assert processing["reviewed-controls"]["sourceName"] == "4096"
assert processing["reviewed-controls"]["targetName"] == "6144"
for field in ["applyStretch", "auditionStretch"]:
    assert not control(tree("20261010202504192", "marker-changed"), field)["enabled"]
    assert control(tree("20261010202504192", "marker-restored"), field)["enabled"]
assert "Already attached" in attached["inventory-attached"]["name"]
assert attached["inventory-attached"]["reviewEnabled"] is False
assert attached["completed"]["nativeAudioActivated"] is False

raw = data(PROJECT + "media/raw-été.wav")
assert raw == captured(render_capture, PROJECT + "media/raw-été.wav")
assert sha(raw) == "3ab3b2a8d0d9219453eae135e9351bb5a66b7f954bed4f3da4233ca0a4059922"
output = data(DERIVED + "audio.wav")
complete = load(DERIVED + "complete.json")
intent = load(DERIVED + "intent.json")
selection_bytes = data(PROJECT + "media/stretch-selections/" + OP + ".json")
selection = json.loads(selection_bytes)
assert sha(selection_bytes) == data(DERIVED + "selection.sha256").decode()
assert selection["request"]["timeNumerator"] == 3 and selection["request"]["timeDenominator"] == 2
assert selection["request"]["warp"] == complete["warp"] == intent["warp"] == anchor["warp"]
assert complete["complete"] and intent["complete"] is False
assert complete["operation"] == intent["operation"] == selection["operation"] == OP
assert complete["renderKey"] == selection["renderKey"] == anchor["renderKey"]
assert complete["sourceSha256"] == sha(raw)
assert complete["writtenFrames"] == complete["target"] == 49152
assert complete["durabilityMinimum"] == "file-flushed"  # Does not prove power-loss durability.
assert complete["payloadPeakBytes"] <= complete["memoryCeilingBytes"] == 268435456
for suffix in ["audio.wav", "complete.json", "intent.json", "selection.sha256"]:
    assert data(DERIVED + suffix) == captured(render_capture, DERIVED + suffix)
assert selection_bytes == captured(render_capture, PROJECT + "media/stretch-selections/" + OP + ".json")
r, raw_pcm, raw_values = wave(raw)
d, derivative_pcm, derivative_values = wave(output)
assert r == {"channels": 2, "rate": 48000, "frames": 32768, "peak": 1.5}
assert d == {"channels": 2, "rate": 48000, "frames": 49152, "peak": 1.5}
assert sha(output) == complete["audioSha256"] and sha(derivative_pcm) == complete["sampleSha256"]
assert raw_pcm[3904 * 8:6080 * 8] == derivative_pcm[5952 * 8:8128 * 8]
assert raw_values[3840 * 2:6144 * 2] == derivative_values[5888 * 2:8192 * 2]
source_span, output_span = raw_pcm[3840 * 8:6144 * 8], derivative_pcm[5888 * 8:8192 * 8]
differences = [(source_span[n:n+4], output_span[n:n+4]) for n in range(0, len(source_span), 4)
               if source_span[n:n+4] != output_span[n:n+4]]
assert len(differences) == 128
assert all({a, b} == {b"\x00\x00\x00\x00", b"\x00\x00\x00\x80"} for a, b in differences)
exports = [data(PROJECT + "exports/restart-export-" + name + ".wav") for name in ["first", "second"]]
assert exports[0] == exports[1]
assert sha(exports[0]) == history["restart-export-first"]["sha256"] == attached["restart-export-second"]["sha256"]
e, export_pcm, export_values = wave(exports[0])
assert e == d and export_values == derivative_values
export_differences = [(export_pcm[n:n+4], derivative_pcm[n:n+4]) for n in range(0, len(export_pcm), 4)
                      if export_pcm[n:n+4] != derivative_pcm[n:n+4]]
assert all({a, b} == {b"\x00\x00\x00\x00", b"\x00\x00\x00\x80"} for a, b in export_differences)

# Retain unsuccessful operators; later success never changes their earlier receipts.
failures = [events(stamp)["operator-failure"] for stamp in [
    "20261010202152514", "20261010202336365", "20261010202504192", "20261010202735184"]]
assert [f["message"] for f in failures] == ["Observation deadline: specific retained job inventory",
    "Recovered controls differ", "Observation deadline: completed WAV file",
    "Observation deadline: specific retained job inventory"]
assert all(f["actualExited"] is False for f in failures)
refused = control(tree("20261010202504192", "failure"), "exportStatus")
assert "Exports inside a project must be in its exports directory" in refused["name"]
state = json.loads(blobs["final-state.json"])
assert " running" not in state["vmState"] and state["ownedCloneOff"]
assert state["receiverActualExit"] == 0 and state["ownedFirewallRuleRemoved"] and state["ownedCdEjected"]
assert not state["originalsModified"] and not state["productUpload"] and not state["nativeAudioActivated"]
assert summary["normalReadyApplyUndoRedoAttachedExportQualified"] is True
for name in ["nativeAuditionQualified", "staleInvalidInstalledWindowsQualified",
             "abruptParentTerminationQualified", "powerLossQualified", "scalableRecoveryQualified",
             "helperModulesQualified", "physicalAudioQualified", "fullQualityQualified",
             "fullGoalComplete", "installerUploaded"]:
    assert summary[name] is False, name
print("Retained Windows normal restart: three actual normal quits; helper/main hashes; Ready/marker/Apply; "
      "saved Undo/Redo; Attached refusal; repeat byte-identical export; raw/core bits and headroom passed. "
      f"Boundary signed zeros: 128; export signed zeros: {len(export_differences)}. No native replay/full parity claim.")
