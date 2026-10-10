#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit retained Windows files only; no archived code, native process or audio replay."""
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
with zipfile.ZipFile(io.BytesIO(packed)) as z:
    assert len(z.namelist()) == len(set(z.namelist())) == 143
    assert set(z.namelist()) == set(manifest)
    blobs = {n: z.read(n) for n in z.namelist()}
for name, b in blobs.items():
    assert manifest[name] == {"bytes": len(b), "sha256": sha(b)}
load = lambda n: json.loads(blobs[n].decode("utf-8-sig"))
assert sha(blobs["operator/probe.cpp"]) == summary["probeSourceSha256"]
assert sha(blobs["operator/final-driver.py"]) == summary["driverSourceSha256"]
initial_inputs, final_inputs = load("operator/compiled-source-inputs.json"), load("final/compile-inputs.json")
assert set(initial_inputs) == set(final_inputs)
changed = [n for n in final_inputs if initial_inputs[n] != final_inputs[n]]
assert changed == summary["changedScriptInputs"] == ["tests/stretch_parent_interruption_windows.py"]
assert initial_inputs[changed[0]]["sha256"] == sha(blobs["initial-lock-refusal/executed-driver.py"])
assert load("operator/compiled-source.json")["head"] == summary["compiledSourceCommit"]
assert load("operator/compiled-source.json")["tree"] == summary["compiledSourceTree"]
assert len(final_inputs) == summary["nativeSourceInputs"] == 613
assert final_inputs["tests/stretch_parent_probe.cpp"]["sha256"] == summary["probeSourceSha256"]
assert final_inputs["tests/stretch_parent_interruption_windows.py"]["sha256"] == summary["driverSourceSha256"]
source = load("final/source.json")
assert source["head"] == summary["executedDriverCommit"] == "8fc9db65a0aa69d3220fdff8583e26d2fa5fe846"
assert source["tree"] == summary["executedDriverTree"] == "3d2c39bc2eb8b3d1df93c9441a781714fbf6ade5"
assert source["unchangedCompiledSourceCommit"] == summary["compiledSourceCommit"] == "46a6d3f8f754e08785f2b841a6d18c1379bc0f5e"
build = load("final/inherited-build-terminal.json")
assert build == load("initial-lock-refusal/build-terminal.json")
assert build["exitCode"] == 0 and build["sourceHead"] == summary["compiledSourceCommit"]
assert build["sourceTree"] == summary["compiledSourceTree"] == "3014b5afc78b93854d0f99872faf0f520a8de40b"
assert build["elapsedSeconds"] == 31.5605293
running, terminal = load("final/running.json"), load("final/terminal.json")
assert terminal["exitCode"] == 0 and not terminal["audioEndpointActivated"]
assert terminal["driverHead"] == source["head"] and terminal["driverTree"] == source["tree"]
assert running["driverHead"] == source["head"] and running["compiledSourceHead"] == build["sourceHead"]
assert running["priorProcessesAbsent"] and not running["compiledInputsChanged"]
assert blobs["observations/final-native-run.log"].count(b"Native source inputs byte unchanged: 613") == 2
report = load("final/observed/report.json")
assert report["platform"] == "Windows" and report["qualification"] == "production-controller-subprocess"
assert report["probeSha256"] == build["probeSha256"] == "7a8f3179a06dc0ddcdcd12392d34a6dbfb1c4225c7d27fe87c94dc96c11c23a4"
assert report["helperSha256"] == build["helperSha256"] == "ca352ca8325219b0dce6a61029878a76f85fc2cdd8d42d75f102b4ba248a2731"
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

stages = ["before-ack", "during-processing", "during-processing-helper-death", "after-complete"]
assert [c["stage"] for c in report["cases"]] == stages and summary["cases"] == 4
parents = set()
for c in report["cases"]:
    stage, op = c["stage"], c["operation"]
    prefix = "final/observed/" + stage + "/project-été-Κиїв/"
    job = prefix + "media/derived/" + op + "/"
    assert c == load("final/observed/" + stage + "/case-report.json")
    parent, terminal_parent = c["parentIdentity"], c["parentTerminal"]
    assert parent["pid"] not in parents and parent["creationFileTime"] > 0
    parents.add(parent["pid"])
    assert parent["sha256"] == report["probeSha256"] and parent["exe"].endswith("\\sc-stretch-parent-probe.exe")
    assert terminal_parent == {"pid": parent["pid"], "creationFileTime": parent["creationFileTime"], "exitCode": 0x53430001}
    cleanup = load("final/observed/" + stage + "/cleanup-terminal.json")
    assert cleanup[0] == terminal_parent
    ticket = load("final/observed/" + stage + "/parent-ready.json")
    assert ticket["pid"] == parent["pid"] and ticket["operation"] == op and not ticket["attached"]
    assert ticket["point"] == ("after-child-before-verification" if stage == "after-complete" else "before-start-acknowledgement")
    for n, h in c["fixtureAfter"].items():
        assert sha(blobs[prefix + n]) == h
    for n, h in c["fixtureBefore"].items():
        assert c["fixtureAfter"][n] == h
    assert c["savedProjectAndRawUnchanged"] and not c["installedGuiQualified"]
    assert not c["audioEndpointActivated"] and not c["powerLossQualified"] and c["boundaryReadRefusals"] == 0
    project = load(prefix + "project.json")
    assert project["tracks"][0]["clips"][0]["stretch"] is None
    selection = blobs[prefix + "media/stretch-selections/" + op + ".json"]
    selected = json.loads(selection)
    assert selected["operation"] == op and selected["format"] == "sc-stretch-selection-v1"
    assert selected["policy"]["deadlineMilliseconds"] == 5000
    assert (selected["request"]["timeNumerator"], selected["request"]["timeDenominator"], selected["request"]["pitchMilliCents"]) == (1, 1, 0)
    raw_pcm, raw_samples = pcm(blobs[prefix + "media/raw-été.wav"])
    if stage != "after-complete":
        helper, terminal_helper = c["helperIdentity"], c["helperTerminal"]
        assert helper["parentPid"] == parent["pid"] and helper["creationFileTime"] > 0
        assert helper["sha256"] == report["helperSha256"] and helper["exe"].endswith("\\sc-stretch-render-worker.exe")
        assert terminal_helper["pid"] == helper["pid"] == ticket["helperPid"]
        assert terminal_helper["creationFileTime"] == helper["creationFileTime"] and cleanup == [terminal_parent, terminal_helper]
        live = c["liveRecovery"]
        assert live["status"] == "Active" and not live["reviewed"] and not live["attached"]
        assert live["operation"] == op and live["projectSha256"] == c["fixtureBefore"]["project.json"]
        assert live["exclusiveLockHashDeferredUntilTerminal"]
        if stage != "before-ack":
            assert len(set(c["ackFrozenThreadIds"])) == len(c["ackFrozenThreadIds"]) >= 2
            assert set(c["partialFrozenThreadIds"]) == set(c["ackFrozenThreadIds"])
            assert c["partialBytesAtParentDeath"] > 4096
    else:
        assert cleanup == [terminal_parent] and c["helperPidReportedByReapingParent"] == ticket["helperPid"]
        assert "helperTerminal" not in c
    complete = stage in ("during-processing", "after-complete")
    recovery = c["terminalRecovery"]
    assert recovery["operation"] == op and recovery["projectSha256"] == c["fixtureBefore"]["project.json"]
    assert recovery["status"] == ("Ready" if complete else "Incomplete") and recovery["reviewed"] == complete
    assert not recovery["attached"]
    if complete:
        receipt = load(job + "complete.json")
        assert receipt["complete"] and receipt["operation"] == op and receipt["writtenFrames"] == 1048576
        assert receipt["audioSha256"] == sha(blobs[job + "audio.wav"])
        assert receipt["sourceSha256"] == sha(blobs[prefix + "media/raw-été.wav"])
        assert receipt["durabilityMinimum"] == "file-flushed" and receipt["peakLinear"] == 2
        assert blobs[job + "selection.sha256"] == sha(selection).encode() and blobs[job + "start.request"] == b"start"
        output_pcm, output_samples = pcm(blobs[job + "audio.wav"])
        assert output_samples == raw_samples and receipt["sampleSha256"] == sha(output_pcm)
        if stage == "during-processing":
            assert terminal_helper["exitCode"] == 0
    else:
        assert job + "complete.json" not in blobs and job + "audio.wav" not in blobs
        if stage == "before-ack":
            assert terminal_helper["exitCode"] == 2 and job + "start.request" not in blobs and job + "audio.partial" not in blobs
        else:
            assert terminal_helper["exitCode"] == 0x53430002
            partial = blobs[job + "audio.partial"]
            assert len(partial) == c["partialBytesAtParentDeath"] and sha(partial) == c["partialPreservedSha256"]
            assert blobs[job + "selection.sha256"] == sha(selection).encode()

assert load("initial-lock-refusal/terminal.json")["failed"]
assert b"PermissionError" in blobs["initial-lock-refusal/host-native-run.log"] and b"writer.lock" in blobs["initial-lock-refusal/host-native-run.log"]
assert load("initial-publication-refusal/terminal.json")["failed"]
assert b"PermissionError" in blobs["initial-publication-refusal/host-native-run.log"] and b"parent-ready.json" in blobs["initial-publication-refusal/host-native-run.log"]
for stage in stages:
    observed_cleanup = load("initial-publication-refusal/observed/" + stage + "/cleanup-terminal.json")
    assert observed_cleanup and observed_cleanup[0]["exitCode"] == 0x53430001
cleanup = load("observations/cleanup.json")
assert cleanup["state"] == "shut off" and cleanup["activeVMs"] == []
assert not cleanup["receiverStarted"] and not cleanup["firewallChanged"] and not cleanup["originalsModified"]
assert load("observations/guest-final-process-state.json")["ownedProbeAndHelperAbsent"]
for key in ["actualWindowsParentInterruptionQualified", "incompletePartialPreserved", "savedProjectAndRawUnchanged",
            "noAutomaticAttachment", "initialLockRefusalPreserved", "initialPublicationRefusalPreserved"]:
    assert summary[key] is True
for key in ["compiledInputsChanged", "publicationRetryObservedInSuccessfulRun", "installedGuiQualified",
            "audioEndpointActivated", "rubberBandQualityQualified", "powerLossQualified", "largeInventoryQualified",
            "fullGoalComplete", "productUploaded"]:
    assert summary[key] is False
print("Windows actual-parent interruption capture: four owned native cases audited; failed operators retained; no archived/native/audio replay")
