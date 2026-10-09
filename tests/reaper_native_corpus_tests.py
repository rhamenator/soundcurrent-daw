#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inspect exact native-writer projects; semantic conversion remains unqualified."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

worker, corpus = (Path(p).resolve() for p in sys.argv[1:3])
manifest = json.loads((corpus / "manifest.json").read_text(encoding="utf-8"))
checks = 0
results = []


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(message)


check(manifest["format"] == "sc-native-writer-corpus-v1" and
      manifest["writer"] == "7.82/linux-x86_64", "Corpus baseline changed")
check(len(manifest["cases"]) == 7, "Native writer cases omitted")
check(not manifest["semanticImportQualified"] and
      not manifest["renderComparisonQualified"] and
      not manifest["windowsWriterQualified"], "Corpus promotes unrelated qualification")
for name, record in manifest["files"].items():
    path = corpus / name
    check(path.is_file() and path.stat().st_size == record["bytes"] and
          hashlib.sha256(path.read_bytes()).hexdigest() == record["sha256"],
          f"Frozen original fixture changed: {name}")

# Independently verify all authored source PCM, rather than treating hashes or
# a producer's expected sample list as proof of original media contents.
for channels, name in ((1, "mono.wav"), (2, "stereo.wav")):
    with wave.open(str(corpus / "projects/media" / name), "rb") as reader:
        check((reader.getnchannels(), reader.getsampwidth(), reader.getframerate(),
               reader.getnframes(), reader.getcomptype()) ==
              (channels, 2, 48000, 96000, "NONE"), "Original PCM shape changed")
        first = 0
        while block := reader.readframes(1024):
            frames = len(block) // (2 * channels)
            expected = tuple(((f * 73 + c * 193) % 4093) - 2046
                             for f in range(first, first + frames)
                             for c in range(channels))
            check(struct.unpack("<" + "h" * len(expected), block) == expected,
                  "Original authored PCM changed")
            first += frames
        check(first == 96000, "Original PCM truncated")

with tempfile.TemporaryDirectory(prefix="sc-native-corpus-") as temporary:
    root = Path(temporary) / "été-Ελληνικά-Київ"
    root.mkdir()
    for case in manifest["cases"]:
        original = corpus / case["file"]
        source = original.read_bytes()
        observed = json.loads((corpus / case["observation"]).read_text(encoding="utf-8"))
        check(observed["beforeSave"] == observed["afterReopen"],
              "Native writer's original save/reopen witness differs")
        witness = observed["afterReopen"]
        check(witness["writer"] == manifest["writer"] and witness["playState"] == 0 and
              witness["projectSampleRate"] == 48000, "Native writer witness changed")
        check(all(t["fxCount"] == 0 for t in witness["tracks"]),
              "Corpus unexpectedly contains instantiated FX")
        # No referenced media is copied: inspection must remain independent of
        # media/plugin resolution. Only this Unicode-named opaque input exists.
        path = root / (case["id"] + "-Ångström.rpp")
        path.write_bytes(source)
        process = subprocess.Popen([str(worker), "--rpp", str(path),
                                    "--memory-bytes", "4194304"],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            output, error = process.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise AssertionError("Native corpus inspection worker exceeded deadline")
        check(process.returncode == 0 and not error, f"Native project refused: {case['id']}: {error!r}")
        report = json.loads(output)
        check(report["protocol"] == "sc-import-inspection-v1" and report["complete"],
              "Native project has no complete versioned report")
        check(report["workerPid"] == process.pid and process.pid != os.getpid(),
              "Native corpus was not inspected by the observed child")
        digest = hashlib.sha256(source).hexdigest()
        check(report["source"]["bytes"] == len(source) and report["source"]["sha256"] == digest,
              "Native original byte identity lost")
        check(report["nativeCompatibility"] == "unqualified" and
              report["semanticStatus"] == "unverified" and
              report["writerVersion"]["status"] == "unverified",
              "A structural outline claimed semantic/native compatibility")
        position, track_count, item_count = 0, 0, 0
        for index, node in enumerate(report["nodes"]):
            begin, length = node["lineRange"]
            check(node["index"] == index and begin == position and length > 0,
                  "Native bytes have a line gap/overlap")
            position += length
            check(node["status"] == "unverified" and node["originalBytesRetained"],
                  "Native unsupported state discarded or falsely converted")
            for field in ("lineRange", "keyRange", "extentRange"):
                b, n = node[field]
                check(0 <= b <= len(source) and 0 <= n <= len(source) - b,
                      "Native structural range escapes retained bytes")
            b, n = node["keyRange"]
            key = source[b:b+n]
            if node["kind"] == "block-open":
                track_count += key == b"TRACK"
                item_count += key == b"ITEM"
        check(position == len(source), "Native original bytes missing from inventory")
        check(track_count == case["trackCount"] == len(witness["tracks"]) and
              item_count == case["itemCount"] == sum(len(t["items"]) for t in witness["tracks"]),
              "Structural objects disagree with actual native writer's API witness")
        check(path.read_bytes() == source and original.read_bytes() == source and
              list(root.iterdir()) == [path], "Worker modified source or resolved media/state")
        path.unlink()
        results.append({"case": case["id"], "workerPid": process.pid,
                        "sourceSha256": digest, "bytes": len(source),
                        "nodes": len(report["nodes"]), "trackBlocks": track_count,
                        "itemBlocks": item_count})

print(json.dumps({"checks": checks, "writer": manifest["writer"],
                  "inspectionPlatform": sys.platform, "cases": results,
                  "nativeWriterExecutedByThisTest": False,
                  "semanticImportQualified": False, "renderComparisonQualified": False}))
