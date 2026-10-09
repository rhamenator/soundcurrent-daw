# SPDX-License-Identifier: GPL-3.0-only
"""Actual inspector -> preserved original/loss state -> portable project reopen.

Original owned synthetic/known-writer fixtures; no suite conversion or audio device.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

inspector, unit = (str(Path(s).resolve()) for s in sys.argv[1:3])
corpus = Path(sys.argv[3]).resolve()
checks = 0


def check(ok, why):
    global checks
    checks += 1
    assert ok, why


def case(root, source, media):
    root.mkdir()
    selected = root / "original.rpp"
    selected.write_bytes(source)
    with subprocess.Popen([inspector, "--rpp-properties", str(selected),
                           "--memory-bytes", "4194304"], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as child:
        output, error = child.communicate(timeout=20)
        check(child.returncode == 0 and not error, "Actual inspection failed")
        report = json.loads(output)
        check(report["workerPid"] == child.pid, "Actual inspector identity lost")
    check(report["protocol"] == "sc-import-inspection-v2", "Property inspection missing")
    encoded = json.dumps(report, ensure_ascii=True, separators=(",", ":")).encode()
    (root / "inspection.scinspect").write_bytes(
        b"SCIBND01" + struct.pack("<QQQ", len(source), len(encoded), child.pid)
        + source + encoded + hashlib.sha256(encoded).hexdigest().encode())
    prop = next(p for p in report["preview"]["properties"] if p["id"] == 20)
    offset, length = prop["valueRange"]
    reference = source[offset:offset + length].decode()
    file = root / reference
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(media)
    result = subprocess.run([unit, str(root)], capture_output=True, timeout=60)
    check(result.returncode == 0, "Actual project state workflow failed: "
          + result.stderr.decode(errors="replace")[:2048])
    check(b"import-state checks" in result.stdout, "Acceptance body did not execute")
    sys.stdout.buffer.write(result.stdout)
    check(selected.read_bytes() == source and not file.exists(),
          "Original preservation/source-absence fixture differs")
    relocated = root / "Relocated — Łódź"
    saved = json.loads((relocated / "project.json").read_text())
    check(saved["schemaMinor"] == 8 and len(saved["imports"]) == 1,
          "Portable import manifest missing")
    source_record = saved["imports"][0]
    check(source_record["sourceSha256"] == hashlib.sha256(source).hexdigest(),
          "Original source digest changed")
    archive = (relocated / source_record["inspection"]["path"]).read_bytes()
    check(source_record["inspection"]["sha256"] == hashlib.sha256(archive).hexdigest()
          and archive[32:32 + len(source)] == source, "Independent source archive bytes differ")
    asset = saved["assets"][0]
    check((relocated / asset["path"]).read_bytes() == media
          and asset["sha256"] == hashlib.sha256(media).hexdigest(),
          "Independent owned audio bytes differ")
    origin = source_record["media"][0]
    receipt = (relocated / origin["receipt"]["path"]).read_bytes()
    check(origin["receipt"]["sha256"] == hashlib.sha256(receipt).hexdigest()
          and json.loads(receipt)["phase"] == 2, "Independent media provenance differs")
    check("originalRoot" not in source_record and json.loads(receipt)["sourceRootsPersisted"] is False,
          "Saved evidence grants historical source authority")


with tempfile.TemporaryDirectory(prefix="sc-import-state-été-") as temporary:
    root = Path(temporary)
    samples = [0.0, -2.5, 1.75, 0.125] * 64
    data = struct.pack("<" + "f" * len(samples), *samples)
    fmt = struct.pack("<HHIIHH", 3, 2, 48000, 384000, 8, 32)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(data)) + data
    wave = b"RIFF" + struct.pack("<I", len(body)) + body
    source = (b'<REAPER_PROJECT 0.1 7.82\n SAMPLERATE 48000 1\n <TRACK\n'
              b'  NAME "Opaque fixture"\n  <ITEM\n   POSITION 0.002\n   LENGTH 0.01\n'
              b'   UNKNOWN_DO_NOT_EXECUTE "foreign-state"\n   <SOURCE WAVE\n'
              b'    FILE "audio.wav"\n   >\n  >\n >\n>\n')
    case(root / "synthetic", source, wave)
    manifest = json.loads((corpus / "manifest.json").read_text())
    seen = []
    for native in manifest["cases"]:
        if native["id"] not in {"unicode-mono", "stereo-gain-pan"}:
            continue
        original = (corpus / native["file"]).read_bytes()
        # Known corpus references are owned repository files. Preserve the actual
        # reference token and exact source bytes; never run the source suite here.
        reference = next(line.strip()[6:-1].decode() for line in original.splitlines()
                         if line.strip().startswith(b'FILE "'))
        media = (corpus / "projects" / reference).read_bytes()
        case(root / native["id"], original, media)
        check((corpus / native["file"]).read_bytes() == original
              and (corpus / "projects" / reference).read_bytes() == media,
              "Known-writer corpus changed")
        seen.append(native["id"])
    check(seen == ["unicode-mono", "stereo-gain-pan"], "Known-writer cases missing")

print(f"PASS: {checks} independent import-state workflow checks; source/loss/provenance persistence, not conversion parity")
