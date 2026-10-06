#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only audit of reviewed equalizer inputs. Never copies or fetches code."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    with path.open("rb") as stream:
        value = hashlib.sha256()
        for block in iter(lambda: stream.read(65536), b""):
            value.update(block)
        return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        help="Parent containing soundcurrent-eq and soundcurrent-studio")
    parser.add_argument("--verify-snapshots", action="store_true",
                        help="Verify retained inputs without needing the equalizer checkouts")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source_root = args.source_root or root.parent
    report = {"snapshot_errors": [], "sources": {}, "review_required": False}
    try:
        review = json.loads((root / "reuse/upstream-review.json").read_text())
        # Old exact revisions remain immutable origins of existing adaptations.
        for manifest in ["reuse/studio/provenance.json", "reuse/equipment/provenance.json"]:
            pinned = json.loads((root / manifest).read_text())
            for value in pinned["files"].values():
                if digest(root / value["snapshot"]) != value["sha256"]:
                    report["snapshot_errors"].append(value["snapshot"])
        for name, repository in review["repositories"].items():
            status = {"reviewed_head": repository["observed_head"], "files": {}}
            report["sources"][name] = status
            if not args.verify_snapshots:
                result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source_root / name,
                                        text=True, capture_output=True, check=False)
                status["current_head"] = result.stdout.strip() if result.returncode == 0 else None
                status["head_changed"] = (status["current_head"] != status["reviewed_head"]
                                          if status["current_head"] is not None else None)
            for relative, value in repository["files"].items():
                if digest(root / value["snapshot"]) != value["sha256"]:
                    report["snapshot_errors"].append(value["snapshot"])
                if args.verify_snapshots:
                    continue
                path = source_root / name / relative
                observed = digest(path) if path.is_file() else None
                changed = observed != value["sha256"]
                status["files"][relative] = {"reviewed_sha256": value["sha256"],
                    "current_sha256": observed, "review_required": changed}
                report["review_required"] |= changed
        print(json.dumps(report, indent=2))
        return 1 if report["snapshot_errors"] else 2 if report["review_required"] else 0
    except (OSError, ValueError, KeyError) as error:
        print(json.dumps({"audit_error": str(error)}), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
