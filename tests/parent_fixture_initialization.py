# SPDX-License-Identifier: GPL-3.0-only
"""Owned fixture-init diagnostics; unchanged deadline and no retry."""
import json
from pathlib import Path
import subprocess
import sys
import time


def initialize_fixture(probe, root, directory, *, creationflags=0):
    directory = Path(directory)
    output = directory / "fixture-init.log"
    begun = time.monotonic_ns()
    result = None
    primary = None
    try:
        with output.open("wb") as log:
            result = subprocess.run([str(probe), "--init", str(root)], check=True,
                                    timeout=10, creationflags=creationflags,
                                    stdout=log, stderr=subprocess.STDOUT)
    except BaseException as error:
        primary = error
        raise
    finally:
        try:
            with output.open("rb") as log:
                prefix = log.read(16384)
            text = prefix.decode("utf-8", errors="replace")
            lines = text.splitlines()  # Preserve raw text while accepting native LF/CRLF.
            record = {"schemaMajor": 1, "timeoutSeconds": 10,
                      "elapsedMonotonicNs": time.monotonic_ns() - begun,
                      "timedOut": isinstance(primary, subprocess.TimeoutExpired),
                      "childReturncode": result.returncode if result else
                          primary.returncode if isinstance(primary, subprocess.CalledProcessError) else None,
                      "outputBytes": output.stat().st_size,
                      "outputTruncated": output.stat().st_size > len(prefix),
                      "hashMarkerObserved": "sc-parent-fixture source.hash-verified" in lines,
                      "saveMarkerObserved": "sc-parent-fixture project-save.end" in lines,
                      "output": text}
            record["diagnosticsComplete"] = (not record["outputTruncated"] and
                                             record["hashMarkerObserved"] and record["saveMarkerObserved"])
            (directory / "fixture-init.json").write_text(
                json.dumps(record, indent=2) + "\n", encoding="utf-8")
            # Explicit child handles work with CREATE_NO_WINDOW too. Replay the
            # retained prefix through the observer's CTest stderr on all paths.
            sys.stderr.write(text)
            if primary is None and not record["diagnosticsComplete"]:
                raise RuntimeError("Fixture initialization diagnostics incomplete")
        except Exception as evidence_error:
            if primary is None:
                raise
            primary.add_note("Fixture initialization evidence: " + repr(evidence_error))

