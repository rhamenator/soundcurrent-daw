#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify the actual audit CLI against isolated changed/missing/corrupt inputs."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="sc-reuse-audit-") as temporary:
    tree = Path(temporary) / "daw"
    sources = Path(temporary) / "sources"
    manifests = ["reuse/studio/provenance.json", "reuse/equipment/provenance.json",
                 "reuse/upstream-review.json"]
    paths = {"tools/check_equalizer_reuse.py", *manifests}
    for manifest in manifests:
        value = json.loads((root / manifest).read_text())
        collections = [r["files"] for r in value["repositories"].values()] if "repositories" in value else [value["files"]]
        for entries in collections:
            paths.update(f["snapshot"] for f in entries.values())
    for relative in paths:
        target = tree / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / relative, target)
    review = json.loads((tree / manifests[-1]).read_text())
    for name, repository in review["repositories"].items():
        for relative, value in repository["files"].items():
            target = sources / name / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(tree / value["snapshot"], target)

    # A source commit alone is informational; changed borrowed bytes are decisive.
    public = sources / "soundcurrent-eq"
    def git(*arguments):
        return subprocess.run(["git", "-c", "user.name=Reuse audit fixture",
                               "-c", "user.email=reuse-fixture@example.invalid", *arguments],
                              cwd=public, capture_output=True, text=True, check=True).stdout.strip()
    git("init", "--quiet")
    git("add", ".")
    git("commit", "--quiet", "-m", "Owned reviewed input fixture")
    review["repositories"]["soundcurrent-eq"]["observed_head"] = git("rev-parse", "HEAD")
    (tree / manifests[-1]).write_text(json.dumps(review))

    def run(*args):
        result = subprocess.run([sys.executable, str(tree / "tools/check_equalizer_reuse.py"), *args],
                                capture_output=True, text=True, check=False)
        return result.returncode, json.loads(result.stdout or result.stderr)

    args = ("--source-root", str(sources))
    code, report = run(*args)
    assert code == 0 and not report["review_required"] and not report["snapshot_errors"], report
    assert report["sources"]["soundcurrent-eq"]["head_changed"] is False, report
    (public / "unrelated.txt").write_text("Independent source work\n")
    git("add", "unrelated.txt")
    git("commit", "--quiet", "-m", "Unrelated source change")
    before_status = git("status", "--porcelain")
    before_head = git("rev-parse", "HEAD")
    code, report = run(*args)
    assert code == 0 and not report["review_required"], report
    assert report["sources"]["soundcurrent-eq"]["head_changed"] is True, report
    assert git("status", "--porcelain") == before_status and git("rev-parse", "HEAD") == before_head

    # The premium editor/header/catalog can diverge independently from the free EQ.
    for relative in ["src/equipment_profiles.h", "scripts/collect-equipment-profiles.py",
                     "data/equipment/spinorama.json", "data/equipment/LICENSE",
                     "data/equipment/collection-report.json", "data/equipment/measurement-sources.json",
                     "tests/equipment_profiles.cpp"]:
        source = sources / "soundcurrent-studio" / relative
        original = source.read_bytes()
        changed_bytes = original + b"\nIndependent premium edit\n"
        source.write_bytes(changed_bytes)
        code, report = run(*args)
        changed = [(name, path) for name, repository in report["sources"].items()
                   for path, value in repository["files"].items() if value["review_required"]]
        assert code == 2 and changed == [("soundcurrent-studio", relative)], report
        assert source.read_bytes() == changed_bytes, "Audit modified source input"
        source.write_bytes(original)
    source = sources / "soundcurrent-eq/src/accelerating_spinbox.h"
    source.write_bytes(source.read_bytes() + b"\n// Independent future source edit\n")
    code, report = run(*args)
    changed = [(name, path) for name, repository in report["sources"].items()
               for path, value in repository["files"].items() if value["review_required"]]
    assert code == 2 and changed == [("soundcurrent-eq", "src/accelerating_spinbox.h")], report
    source.unlink()
    code, report = run(*args)
    assert code == 2 and report["sources"]["soundcurrent-eq"]["files"]["src/accelerating_spinbox.h"]["current_sha256"] is None, report
    code, report = run("--verify-snapshots")
    assert code == 0, report
    snapshot = tree / review["repositories"]["soundcurrent-eq"]["files"]["src/accelerating_spinbox.h"]["snapshot"]
    snapshot.write_bytes(snapshot.read_bytes() + b"\n// Corrupt retained origin\n")
    code, report = run("--verify-snapshots")
    assert code == 1 and str(snapshot.relative_to(tree)) in report["snapshot_errors"], report
print("Reuse audit passed: matching inputs, informational HEAD changes, seven independent premium inputs, source preservation, changed/missing file, standalone integrity, corrupted snapshot.")
