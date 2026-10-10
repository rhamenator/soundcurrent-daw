#!/usr/bin/env python3
"""Verify retained observations only; no Windows, DSP or product replay."""

from pathlib import Path
import hashlib
import io
import json
import zipfile


ROOT = Path(__file__).resolve().parent
HEAD = "4fb8c1cdf1b1365e5834256f4b39b51566dbed52"
TREE = "d1b6be0bd9114af21d73a5dd6ba8172b53510c44"
summary = json.loads((ROOT / "summary.json").read_text())
manifest = json.loads((ROOT / "capture-members.json").read_text())
archive = (ROOT / "capture.zip").read_bytes()
assert hashlib.sha256(archive).hexdigest() == summary["captureSha256"]
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    names = z.namelist()
    assert len(names) == len(set(names)) and set(names) == set(manifest)
    blobs = {name: z.read(name) for name in names}
for name, data in blobs.items():
    assert len(data) == manifest[name]["bytes"], name
    assert hashlib.sha256(data).hexdigest() == manifest[name]["sha256"], name
    assert ".." not in Path(name).parts and not name.startswith("/"), name
    assert not name.endswith((".exe", ".dll", ".iso")), name
    assert not data.startswith(b"MZ"), name
    assert "private-transport" not in name and "CREDENTIALS" not in name, name


def native(name):
    return json.loads(blobs["native/" + name].decode("utf-8-sig"))


def captured(archive_name, suffix):
    with zipfile.ZipFile(io.BytesIO(blobs["captures/" + archive_name])) as z:
        matches = [n for n in z.namelist() if n.replace("\\", "/").endswith(suffix)]
        assert len(matches) == 1, (archive_name, suffix)
        return z.read(matches[0])


for name, data in blobs.items():
    if name.startswith("captures/"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for member in z.namelist():
                assert not member.endswith((".exe", ".dll", ".iso")), member
                assert "private-transport" not in member and "CREDENTIALS" not in member

build = native("build-terminal.json")
terminal = native("qualification-terminal.json")
v4 = native("stretch-worker-qualification.json")
v5 = native("protected-warp-qualification.json")
for record in (build, terminal, v4, v5):
    assert record["sourceCommit"] == HEAD and record["sourceTree"] == TREE
    assert record["nativeAudio"] is False
assert build["exitCode"] == terminal["exitCode"] == 0
assert (v4["checks"], v4["verifierChecks"]) == (385, 1045)
assert v4["exitCode"] == v4["verifierExitCode"] == 0
assert (v5["checks"], v5["completedRenders"], len(v5["comparisons"])) == (519, 21, 18)
assert v5["fullQualityQualified"] is False
assert all(c["allPcmEqual"] for c in v5["comparisons"])
assert v5["workerSha256"] == build["exeSha256"]["sc-stretch-render-worker"]
diagnostics = native("diagnostic-current.json")
assert len(diagnostics) == 2
for d in diagnostics:
    assert d["sourceCommit"] == HEAD and d["diagnosticOnly"] is True
    outcome = d["outcome"]
    assert outcome["exitCode"] == 0 and outcome["seconds"] < 10
    assert not outcome["observationTimedOut"] and not outcome["retirementPending"]
    assert not outcome["outputLimited"]
collection = native("collection-state.json")
assert collection["sourceCommit"] == HEAD and collection["sourceTree"] == TREE
assert collection["sourceClean"] and collection["processes"] == []

close = json.loads(captured("final-normal-close-4fb8.zip", "final-normal-close.json").decode("utf-8-sig"))
main = json.loads(captured("main-4fb8-20261010190524920.zip", "main-20261010190524920.json").decode("utf-8-sig"))
setup = json.loads(captured("setup-4fb8.zip", "setup-result.json").decode("utf-8-sig"))
assert main["sourceHead"] == setup["sourceHead"] == HEAD
assert main["pid"] == close["pid"] == 5076 and main["exe"] == close["path"]
assert close["hash"].lower() == main["exeSha256"] == build["exeSha256"]["soundcurrent-daw"]
assert close["normalCloseRequested"] and close["exited"]
assert close["exitCode"] == 0 and close["forced"] is False
assert setup["setupPid"] == 260 and setup["setupExit"] == 0
assert setup["verifiedPayloadFiles"] == 64 and setup["pristineOs"] is False
assert setup["compilerOnPath"] is setup["qtSdkOnPath"] is setup["nativeAudioActivated"] is False
raw = captured("final-normal-close-4fb8.zip", "media/raw-été.wav")
assert raw == captured("action-4fb8-open.zip", "media/raw-été.wav")
assert hashlib.sha256(raw).hexdigest() == summary["rawMediaSha256"]
assert b"Unsupported Pattern" in captured("final-normal-close-4fb8.zip", "select-script.log")
audit = json.loads(blobs["host/source-archive-verification.json"])
assert audit["sourceHead"] == HEAD and audit["sourceTree"] == TREE
assert audit["trackedMembersVerified"] == 2235 and audit["everyTrackedBlobAndModeVerified"]
assert audit == summary["sourceArchiveAudit"]
assert summary["installedRestartQualified"] is False
assert summary["productReleaseUploaded"] is False and summary["fullGoalComplete"] is False
state = json.loads((ROOT / "final-vm-state.json").read_text())
assert "running" not in state["allVmState"] and "shut off" in state["allVmState"]
assert state["receiverExited"] and state["ownedFirewallRuleRemoved"]
print("Retained native receipts, installation, normal exit, raw/source identities and open scope verified; no replay.")
