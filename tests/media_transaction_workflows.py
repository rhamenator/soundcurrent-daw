# SPDX-License-Identifier: GPL-3.0-only
"""Real inspector -> owned bundle -> actual commit/recovery children and faults."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import uuid

inspector, worker, unit = map(lambda s: str(Path(s).resolve()), sys.argv[1:4])
corpus = Path(sys.argv[4]).resolve()
checks = 0

def check(ok, message):
    global checks
    checks += 1
    assert ok, message

def child(args, accepted=True):
    with subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        output, error = process.communicate(timeout=20)
        check((process.returncode == 0) == accepted, "Wrong actual transaction child exit: " + error.decode(errors="replace")[:1024])
        if accepted:
            check(not error, "Successful child emitted error")
        return output, error, process.pid

def inspect(root, source):
    selected = root / (str(uuid.uuid4()) + ".rpp")
    selected.write_bytes(source)
    output, _, pid = child([inspector, "--rpp-properties", str(selected), "--memory-bytes", "4194304"])
    report = json.loads(output)
    check(report["workerPid"] == pid and report["protocol"] == "sc-import-inspection-v2", "Not actual property inspector")
    encoded = json.dumps(report, ensure_ascii=True, separators=(",", ":")).encode()
    bundle = selected.with_suffix(".scinspect")
    bundle.write_bytes(b"SCIBND01" + struct.pack("<QQQ", len(source), len(encoded), pid) + source + encoded + hashlib.sha256(encoded).hexdigest().encode())
    return bundle, report

def project(fields, source_type="WAVE", take=""):
    return ("<REAPER_PROJECT 0.1 7.82\n SAMPLERATE 48000 1\n <TRACK\n  <ITEM\n" + take + "   <SOURCE " + source_type + "\n" + fields + "   >\n  >\n >\n>\n").encode()

with tempfile.TemporaryDirectory(prefix="sc-transaction-été-Κиїв-") as temporary:
    root = Path(temporary)
    destination = root / "destination"
    destination.mkdir()
    samples = [0.0, -2.5, 1.75, 0.125] * 10000
    data = struct.pack("<" + "f" * len(samples), *samples)
    fmt = struct.pack("<HHIIHH", 3, 2, 48000, 384000, 8, 32)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    wave = b"RIFF" + struct.pack("<I", len(body)) + body
    (root / "audio.wav").write_bytes(wave)
    source = project('    FILE "audio.wav"\n')
    bundle, inspected = inspect(root, source)
    (root / "inspection.scinspect").write_bytes(bundle.read_bytes())
    source_property = next(i for i, p in enumerate(inspected["preview"]["properties"]) if p["id"] == 20)

    output, _, _ = child([unit, str(root)])
    check(b"media transaction checks" in output, "Actual C++ failure/recovery tests did not execute")
    sys.stdout.buffer.write(output)

    def commit(saved=bundle, prop=source_property, relative="audio.wav", selection="original", operation=None, accepted=True):
        operation = operation or str(uuid.uuid4())
        output, error, pid = child([worker, "--commit", "--bundle", str(saved), "--property", str(prop),
            "--root", str(root), "--relative", relative, "--destination", str(destination),
            "--operation", operation, "--selection", selection, "--maximum-bytes", "1048576"], accepted)
        check(len(output) <= 8192 and len(error) <= 1024, "Transaction worker exceeded banks")
        if accepted:
            report = json.loads(output)
            check(report["protocol"] == "sc-media-import-v1" and report["workerPid"] == pid, "Commit PID/protocol mismatch")
            check(report["committed"] is True and report["sessionAssetPublished"] is False and report["phase"] == 2, "Commit claimed session conversion or lost publication")
            check(report["operation"] == operation and report["relative"] == operation + "/media.wav", "Foreign path became owned output")
            check(report["sha256"] == hashlib.sha256(wave).hexdigest() and report["inspectionSha256"] == hashlib.sha256(source).hexdigest(), "Wrong actual copy/original-project hashes")
            return report
        check(not output and json.loads(error)["committed"] is False, "Refused precommit published result")
        return operation

    def recover(operation, accepted=True):
        output, error, pid = child([worker, "--recover", "--destination", str(destination), "--operation", operation,
                                   "--maximum-bytes", "1048576"], accepted)
        if accepted:
            report = json.loads(output)
            check(report["workerPid"] == pid and report["sessionAssetPublished"] is False, "Recovery PID/scope mismatch")
            return report
        check(not output and json.loads(error)["committed"] is False, "Recovery refused data but published success")

    interrupted = str(uuid.uuid4())
    ready = root / "interruption-ready.log"
    with ready.open("wb") as output_file:
        with subprocess.Popen([unit, str(root), "--interrupt", interrupted], stdout=output_file, stderr=subprocess.PIPE) as parked:
            try:
                deadline = time.monotonic() + 15
                while ready.read_bytes() not in (b"ready-before-commit\n", b"ready-before-commit\r\n") and parked.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                check(parked.poll() is None and ready.read_bytes() in (b"ready-before-commit\n", b"ready-before-commit\r\n"), "Abrupt-interruption helper did not reach live precommit boundary")
            finally:
                if parked.poll() is None:
                    parked.terminate()
                _, error = parked.communicate(timeout=5)
            check(parked.returncode != 0 and not error, "Parked child was not abruptly terminated")
    partial = destination / interrupted
    check((partial / "intent.json").exists() and (partial / "receipt.partial").exists() and not (partial / "receipt.json").exists(), "Killed child published a completed receipt")
    check(recover(interrupted)["phase"] == 1 and (partial / "media.wav").read_bytes() == wave, "Fresh recovery child cannot distinguish killed precommit state")

    report = commit()
    operation = report["operation"]
    owned = destination / operation
    intent = json.loads((owned / "intent.json").read_bytes())
    receipt = json.loads((owned / "receipt.json").read_bytes())
    check(intent["phase"] == 1 and receipt["phase"] == 2, "Intent/commit phases not distinct")
    normalized = receipt.copy()
    normalized["phase"] = 1
    check(normalized == intent, "Published receipt changed planned provenance")
    check(bytes.fromhex(receipt["originalReferenceHex"]) == b"audio.wav" and receipt["sourceProperty"] == source_property, "Exact occurrence or original token lost")
    check(receipt["peakLinear"] == 2.5 and receipt["sourceRootsPersisted"] is False and not (owned / "media.partial").exists(), "Headroom/authority/naming policy differs")
    check((owned / "media.wav").read_bytes() == wave and (root / "audio.wav").read_bytes() == wave, "Actual copy differs or source changed")
    check(recover(operation)["phase"] == 2, "Fresh child cannot recover committed audio")
    commit(operation=operation, accepted=False)
    check((owned / "media.wav").read_bytes() == wave, "Operation collision overwritten")
    commit(prop=0, accepted=False)
    commit(relative="../audio.wav", accepted=False)
    commit(relative="absent.wav", accepted=False)

    # Inspect each failing occurrence with an actual parser, rather than forge its status.
    for fields, source_type, take in [
        ('    FILE "audio.wav"\n    FILE "second.wav"\n', "WAVE", ""),
        ('    FILE "audio.wav"\n', "MIDI", ""),
        ('    FILE "audio.wav"\n', "WAVE", '   TAKE\n')]:
        saved, parsed = inspect(root, project(fields, source_type, take))
        properties = [i for i, p in enumerate(parsed["preview"]["properties"]) if p["id"] == 20]
        for prop in properties:
            before = sorted(p.name for p in destination.iterdir())
            commit(saved=saved, prop=prop, selection="replacement", accepted=False)
            check(sorted(p.name for p in destination.iterdir()) == before, "Ambiguous occurrence mutated destination")

    # Missing reference requires an explicit replacement. Preserve original empty token.
    saved, parsed = inspect(root, project(""))
    missing = next(i for i, p in enumerate(parsed["preview"]["properties"]) if p["id"] == 20)
    commit(saved=saved, prop=missing, accepted=False)
    # Hash for this new project differs, so verify this result separately.
    replacement = str(uuid.uuid4())
    output, _, pid = child([worker, "--commit", "--bundle", str(saved), "--property", str(missing), "--root", str(root),
        "--relative", "audio.wav", "--destination", str(destination), "--operation", replacement,
        "--selection", "replacement", "--maximum-bytes", "1048576"])
    replacement_report = json.loads(output)
    check(replacement_report["workerPid"] == pid and replacement_report["committed"], "Missing explicit replacement not committed")
    missing_receipt = json.loads((destination / replacement / "receipt.json").read_bytes())
    check(missing_receipt["originalReferenceHex"] == "" and missing_receipt["selectionKind"] == 2 and missing_receipt["sourcePropertyNode"] == 2**64 - 1, "Missing-reference provenance fabricated an original")

    # Two frozen projects actually saved/reopened by REAPER 7.82 earlier.
    # This test executes our worker on that corpus; it does not execute REAPER
    # or qualify track/gain/fade/timing conversion or aligned suite renders.
    manifest = json.loads((corpus / "manifest.json").read_text())
    corpus_cases = []
    for case in manifest["cases"]:
        if case["id"] not in {"unicode-mono", "stereo-gain-pan"}:
            continue
        native_source = (corpus / case["file"]).read_bytes()
        saved, parsed = inspect(root, native_source)
        prop = next(i for i, p in enumerate(parsed["preview"]["properties"]) if p["id"] == 20)
        offset, length = parsed["preview"]["properties"][prop]["valueRange"]
        reference = native_source[offset:offset + length].decode()
        original_media = (corpus / "projects" / reference).read_bytes()
        copied_id = str(uuid.uuid4())
        output, _, pid = child([worker, "--commit", "--bundle", str(saved), "--property", str(prop),
            "--root", str(corpus / "projects"), "--relative", reference, "--destination", str(destination),
            "--operation", copied_id, "--selection", "original", "--maximum-bytes", "1048576"])
        copied_report = json.loads(output)
        check(copied_report["workerPid"] == pid and copied_report["committed"], "Known-writer actual media commit failed")
        check(copied_report["inspectionSha256"] == hashlib.sha256(native_source).hexdigest() and copied_report["sha256"] == hashlib.sha256(original_media).hexdigest(), "Known-writer provenance hash differs")
        check((destination / copied_id / "media.wav").read_bytes() == original_media, "Known-writer media changed")
        check(recover(copied_id)["phase"] == 2 and (corpus / case["file"]).read_bytes() == native_source and (corpus / "projects" / reference).read_bytes() == original_media, "Known-writer recovery/source preservation failed")
        corpus_cases.append(case["id"])
    check(corpus_cases == ["unicode-mono", "stereo-gain-pan"], "Known-writer workflow cases missing")

    # Reopen uses only selected owned parent, intent, receipt and media. No source access.
    source_bytes = (root / "audio.wav").read_bytes()
    (root / "audio.wav").unlink()
    for path in root.glob("*.scinspect"):
        path.unlink()
    check(recover(operation)["phase"] == 2 and recover(replacement)["phase"] == 2, "Recovery depends on original source/bundle")
    (root / "audio.wav").write_bytes(source_bytes)
    (owned / "media.wav").write_bytes(wave[:-1])
    recover(operation, accepted=False)
    (owned / "media.wav").write_bytes(wave)
    (owned / "receipt.json").write_text('{"phase":2}')
    recover(operation, accepted=False)
    check(recover(str(uuid.uuid4()))["phase"] == 0, "Missing operation became complete")
    check(not (destination / "project.json").exists(), "Development worker published Session state")

print(f"Passed {checks} actual media transaction workflow checks")
