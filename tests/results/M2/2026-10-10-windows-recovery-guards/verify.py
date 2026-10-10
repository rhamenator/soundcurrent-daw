#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit original installed Windows guard observations; no native/script/audio replay."""
from pathlib import Path
import copy
import hashlib
import io
import json
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent / "2026-10-10-native-restart-qualified"))
from capture_safety import check_member

sha = lambda b: hashlib.sha256(b).hexdigest()
summary = json.loads((ROOT / "summary.json").read_bytes())
manifest = json.loads((ROOT / "capture-members.json").read_bytes())
packed = (ROOT / "capture.zip").read_bytes()
assert sha(packed) == summary["captureSha256"]
check_member("capture.zip", packed)
with zipfile.ZipFile(io.BytesIO(packed)) as outer:
    assert len(outer.namelist()) == len(set(outer.namelist())) == 6
    assert set(outer.namelist()) == set(manifest)
    blobs = {n: outer.read(n) for n in outer.namelist()}
for name, b in blobs.items():
    assert manifest[name] == {"bytes": len(b), "sha256": sha(b)}

with zipfile.ZipFile(io.BytesIO(blobs["captures/guards-complete-20261010205716841.zip"])) as capture:
    def data(suffix):
        found = [n for n in capture.namelist() if n.replace("\\", "/").endswith(suffix)]
        assert len(found) == 1, suffix
        return capture.read(found[0])

    def load(suffix):
        return json.loads(data(suffix).decode("utf-8-sig"))

    def control(stage, name):
        rows = load("records/uia-restart-20261010205716841-" + stage + ".json")
        found = [r for r in rows if r["id"].endswith("." + name) and r["class"]]
        assert len(found) == 1, (stage, name)
        return found[0]

    rows = load("records/guards-20261010205716841.json")
    events = {r["name"]: r["data"] for r in rows}
    assert len(rows) == len(events) and "operator-failure" not in events
    assert rows[-1]["name"] == "completed"
    head = "4fb8c1cdf1b1365e5834256f4b39b51566dbed52"
    assert summary["sourceHead"] == events["completed"]["sourceHead"] == head
    assert summary["sourceTree"] == "d1b6be0bd9114af21d73a5dd6ba8172b53510c44"
    assert summary["installedSequence"] == "20261010183111"
    pids = [8452, 9012, 2832]
    assert summary["mainPids"] == pids + [2120]
    expected_names = {"ready": "Ready for review", "stale": "Target changed or missing",
                      "invalid": "Invalid or missing files"}
    operation = "eee85921-5fd6-497f-88be-ecfd1a260bca"
    assert summary["operation"] == operation
    initial_projects = {}
    for case, pid in zip(expected_names, pids):
        launch, quit = events["launch-" + case], events["quit-" + case]
        assert launch["pid"] == quit["pid"] == pid and launch["sessionId"] != 0
        assert launch["sha256"] == "2302a5d5d2da63476a2f1f7074053b64d75a379d31973916bc35881ae29d57bd"
        assert not launch["developerSdkUsed"] and not launch["nativeAudioActivated"]
        install = launch["exe"].rsplit("\\", 1)[0].casefold() + "\\"
        assert install.endswith("20261010183111-4fb8c1cdf1b1\\")
        for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "sndfile.dll", "qwindows.dll"]:
            found = [m for m in launch["modules"] if m["name"] == name]
            assert len(found) == 1 and found[0]["path"].casefold().startswith(install)
        assert quit["actualExited"] and quit["normalCloseRequested"] and not quit["forced"]
        assert quit["exitCode"] == 0 and quit["startUtc"] == launch["startUtc"]
        inventory = events["inventory-" + case]
        assert expected_names[case] in inventory["name"]
        assert inventory["reviewEnabled"] == (case == "ready")
        assert control("scan-" + case, "reviewRetainedStretch")["enabled"] == (case == "ready")
        receipt = events["case-" + case]
        assert receipt["sourceHead"] == head and receipt["installedSequence"] == "20261010183111"
        assert receipt["operation"] == operation
        before = load("records/fixture-" + case + "-before.json")
        after = load("records/fixture-" + case + "-after.json")
        assert before == after
        fixture = "project-" + case + "-été-Κиїв/"
        for file in after:
            b = data(fixture + file["path"].replace("\\", "/"))
            assert len(b) == file["bytes"] and sha(b) == file["sha256"]
        current = data(fixture + "project.json")
        assert current == data("records/project-restart-20261010205716841-opened-" + case + ".json")
        assert current == data("records/project-restart-20261010205716841-final-" + case + ".json")
        initial_projects[case] = json.loads(current.decode("utf-8-sig"))
        assert sha(data(fixture + "media/raw-été.wav")) == "3ab3b2a8d0d9219453eae135e9351bb5a66b7f954bed4f3da4233ca0a4059922"
        assert sha(data(fixture + "media/derived/" + operation + "/audio.wav")) == "d2d6bbfee72fc0b348c73cbaa5d2c6fc409c38960ed368b6ff84f9c7fc2ad703"
        assert data(fixture + "media/derived/" + operation + "/selection.sha256") == b"77eba69a5cf3ff96afba7201748359d6bc8017204e7cb86ccb962dbe43faa183"
    assert initial_projects["ready"] == initial_projects["invalid"]
    stale = copy.deepcopy(initial_projects["stale"])
    assert stale["tracks"][0]["clips"][0]["startFrame"] == 1
    stale["tracks"][0]["clips"][0]["startFrame"] = 0
    assert stale == initial_projects["ready"]
    assert initial_projects["ready"]["tracks"][0]["clips"][0]["stretch"] is None
    assert sha(data("project-ready-été-Κиїв/project.json")) == "86e190d37d3208ea8c9705a40e0d51b7e84e1c23420800089db1a2c251ab76d3"
    original = data("records/original-selection.json")
    assert sha(original) == "77eba69a5cf3ff96afba7201748359d6bc8017204e7cb86ccb962dbe43faa183"
    altered = data("records/altered-selection.json")
    assert sha(altered) != sha(original)
    restored = json.loads(altered.decode("utf-8-sig"))
    assert restored["request"]["timeNumerator"] == 499999 and restored["request"]["timeDenominator"] == 333333
    restored["request"]["timeNumerator"], restored["request"]["timeDenominator"] = 3, 2
    original_json = json.loads(original)
    # The original PowerShell operator also misdecoded BOM-free UTF-8 as ANSI.
    # Preserve and audit that contamination; it is not a ratio-only witness.
    assert restored["request"]["relative"] == original_json["request"]["relative"].encode("utf-8").decode("cp1252")
    assert restored["selection"] == original_json["selection"].encode("utf-8").decode("cp1252")
    restored["request"]["relative"] = original_json["request"]["relative"]
    restored["selection"] = original_json["selection"]
    assert restored == original_json
    for case in expected_names:
        m = data("project-" + case + "-été-Κиїв/media/stretch-selections/" + operation + ".json")
        assert m == (altered if case == "invalid" else original)
    original_before, original_after = load("records/original-before.json"), load("records/original-after.json")
    assert original_before == original_after
    lookup = {r["path"].replace("\\", "/"): r for r in original_after}
    assert lookup["project.json"]["sha256"] == "0b57ba4b6de8a6627fd5de05c164853969e478be870e8e8c51f505aedcb55f3d"
    for case in expected_names:
        for path, file in lookup.items():
            if path not in ("project.json", "media/stretch-selections/" + operation + ".json"):
                assert sha(data("project-" + case + "-été-Κиїв/" + path)) == file["sha256"]
    for name in ["applyStretch", "auditionStretch"]:
        assert control("ready-reviewed", name)["enabled"]
    assert control("audition-prepared", "auditionStretch")["name"] == "Play audition"
    assert control("audition-prepared", "stopStretchAudition")["enabled"]
    assert not control("audition-prepared", "applyStretch")["enabled"]
    assert control("audition-stopped", "auditionStretch")["name"] == "Prepare audition"
    assert not control("audition-stopped", "stopStretchAudition")["enabled"]
    assert control("audition-stopped", "applyStretch")["enabled"]
    assert events["audition-prepared"]["pid"] == events["audition-stopped"]["pid"] == 8452
    assert "Audition — project unchanged" in events["audition-prepared"]["status"]
    assert "0.00 s" in events["audition-prepared"]["status"]
    for name in ["outputChannel0", "outputChannel1"]:
        assert control("audition-prepared", name)["value"] == "Choose an endpoint…"
    assert control("audition-prepared", "outputPeakLabel")["name"] == "Output: −∞ dBFS"
    assert not events["audition-prepared"]["playRequested"] and not events["audition-stopped"]["playRequested"]
    assert events["completed"]["originalUnchanged"] and not events["completed"]["newRenderRequested"]
    assert not events["completed"]["playRequested"] and not events["completed"]["fullGoalComplete"]

with zipfile.ZipFile(io.BytesIO(blobs["captures/guards-corrected-20261010210550204.zip"])) as corrected_capture:
    def corrected_data(suffix):
        found = [n for n in corrected_capture.namelist() if n.replace("\\", "/").endswith(suffix)]
        assert len(found) == 1, suffix
        return corrected_capture.read(found[0])

    def corrected_load(suffix):
        return json.loads(corrected_data(suffix).decode("utf-8-sig"))

    corrected_events = {r["name"]: r["data"] for r in corrected_load("records/guards-20261010210550204.json")}
    assert "operator-failure" not in corrected_events
    launch, quit = corrected_events["launch-invalid-utf8"], corrected_events["quit-invalid-utf8"]
    assert launch["pid"] == quit["pid"] == 2120
    assert launch["sha256"] == "2302a5d5d2da63476a2f1f7074053b64d75a379d31973916bc35881ae29d57bd"
    assert quit["actualExited"] and quit["normalCloseRequested"] and not quit["forced"] and quit["exitCode"] == 0
    assert quit["startUtc"] == launch["startUtc"] and launch["sessionId"] != 0
    assert not launch["developerSdkUsed"] and not launch["nativeAudioActivated"]
    install = launch["exe"].rsplit("\\", 1)[0].casefold() + "\\"
    assert install.endswith("20261010183111-4fb8c1cdf1b1\\")
    for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "sndfile.dll", "qwindows.dll"]:
        found = [m for m in launch["modules"] if m["name"] == name]
        assert len(found) == 1 and found[0]["path"].casefold().startswith(install)
    corrected_rows = corrected_load("records/uia-restart-20261010210550204-scan-invalid-utf8.json")
    reviews = [r for r in corrected_rows if r["id"].endswith(".reviewRetainedStretch") and r["class"]]
    assert len(reviews) == 1 and not reviews[0]["enabled"]
    corrected = corrected_load("records/corrected-selection.json")
    assert (corrected["request"]["timeNumerator"], corrected["request"]["timeDenominator"]) == (499999, 333333)
    corrected["request"]["timeNumerator"], corrected["request"]["timeDenominator"] = 3, 2
    assert corrected == original_json  # All paths and embedded selection bytes preserved.
    inventory = corrected_events["inventory-invalid-utf8"]
    assert inventory["name"] == "Invalid or missing files" and not inventory["reviewEnabled"]
    assert corrected_events["completed"]["fixtureUnchanged"]
    assert not corrected_events["completed"]["playRequested"] and not corrected_events["completed"]["newRenderRequested"]
    before, after = corrected_load("records/fixture-invalid-utf8-before.json"), corrected_load("records/fixture-invalid-utf8-after.json")
    assert before == after
    fixture = "project-invalid-utf8-été-Κиїв/"
    current = corrected_data(fixture + "project.json")
    for stage in ["opened-invalid-utf8", "final-invalid-utf8"]:
        runtime_snapshot = corrected_data("records/project-restart-20261010210550204-" + stage + ".json")
        assert runtime_snapshot == current, ("Corrected runtime project differs from fixture", stage)
    for file in after:
        b = corrected_data(fixture + file["path"].replace("\\", "/"))
        assert sha(b) == file["sha256"] and len(b) == file["bytes"]
    assert corrected_load(fixture + "project.json") == initial_projects["ready"]
    assert corrected_data(fixture + "media/derived/" + operation + "/selection.sha256") == sha(original).encode()
    for path, file in lookup.items():
        if path not in ("project.json", "media/stretch-selections/" + operation + ".json"):
            assert sha(corrected_data(fixture + path)) == file["sha256"]
    expected_manifest = corrected_data("records/corrected-selection.json")
    assert expected_manifest == corrected_data(fixture + "media/stretch-selections/" + operation + ".json")
    assert sha(expected_manifest) != sha(original)

state = json.loads(blobs["final-state.json"])
assert state["ownedCloneOff"] and " running" not in state["allVmState"]
assert state["receiverActualExit"] == 0 and state["ownedFirewallRuleRemoved"] and state["ownedCdEjected"]
assert state["receiverActualExits"] == [0, 0]
assert not state["originalsModified"] and not state["productUpload"]
for name in ["controlSideAuditionPrepareStopQualified", "staleInvalidInstalledWindowsQualified",
             "projectFixturesUnchanged", "provenOriginalProjectUnchanged", "ratioOnlyAlteredManifestQualified",
             "initialEncodingContaminatedFixturePreserved"]:
    assert summary[name] is True
for name in ["newRenderRequested", "nativeAuditionPacketPlaybackQualified", "audiblePlaybackQualified",
             "physicalAudioQualified", "abruptParentTerminationQualified", "powerLossQualified",
             "scalableRecoveryQualified", "fullQualityQualified", "fullGoalComplete", "installerUploaded"]:
    assert summary[name] is False
print("Retained installed Windows recovery guards: four actual normal quits; Ready Prepare/Stop attachment "
      "barrier; stale target and altered manifest refused; all fixture/original file hashes unchanged. "
      "No endpoint selected, no Play, no native packet/physical/power-loss/full-quality claim.")
