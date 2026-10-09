#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real worker -> independent decoder -> portable bundle; no project conversion."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

worker, probe, corpus = map(lambda p: Path(p).resolve(), sys.argv[1:4])
checks = 0
cases = []


def check(ok, message):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(message)


def bundle(path, source, report, pid):
    encoded = json.dumps(report, separators=(",", ":"), ensure_ascii=True).encode()
    path.write_bytes(b"SCIBND01" + struct.pack("<QQQ", len(source), len(encoded), pid) +
                     source + encoded + hashlib.sha256(encoded).hexdigest().encode())


def decoded(path, accepted=True):
    result = subprocess.run([str(probe), str(path)], capture_output=True, timeout=15)
    check((result.returncode == 0) == accepted, "Independent property decoder accepted wrong scope")
    if accepted:
        check(not result.stderr, "Property decoder emitted an error on success")
        return json.loads(result.stdout)
    check(not result.stdout and result.stderr.startswith(b"refused="), "Corrupt report published partial properties")


def property_value(result, object_id, field_id, expected, status=0):
    found = [p for p in result["properties"] if p["object"] == object_id and p["id"] == field_id]
    check(len(found) == 1, "Native field disappeared or duplicated")
    value = found[0]
    check(value["status"] == status, "Original/unsupported status changed at parent boundary")
    if isinstance(expected, str):
        check(value["kind"] == 2 and value["bytesHex"] == expected.encode().hex(), "Native original byte token differs")
    else:
        check(value["kind"] == 1 and value["number"] == expected, "Native API scalar differs after parent decode")


with tempfile.TemporaryDirectory(prefix="sc-property-report-") as temporary:
    root = Path(temporary) / "été-Δοκιμή-Київ"
    root.mkdir()
    manifest = json.loads((corpus / "manifest.json").read_text())
    for case in manifest["cases"]:
        source = (corpus / case["file"]).read_bytes()
        witness = json.loads((corpus / case["observation"]).read_text())["afterReopen"]
        selected = root / (case["id"] + ".rpp")
        selected.write_bytes(source)
        child = subprocess.Popen([str(worker), "--rpp-properties", str(selected),
                                  "--memory-bytes", "4194304"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            output, error = child.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise AssertionError("Property worker exceeded bounded deadline")
        check(child.returncode == 0 and not error, "Original native project property worker refused")
        report = json.loads(output)
        check(report["protocol"] == "sc-import-inspection-v2" and report["workerPid"] == child.pid,
              "Property protocol/PID does not match real child")
        check(all(b < 128 for b in output), "Worker echoed foreign names/path/programs into its protocol")
        saved = root / "inspection-Ångström.scinspect"
        bundle(saved, source, report, child.pid)
        result = decoded(saved)
        check(result["pid"] == child.pid and result["sha256"] == hashlib.sha256(source).hexdigest() and
              result["propertiesVersion"] == 1 and result["retiredBytes"] == 0, "Source/provenance/ownership changed")
        check(len(result["lines"]) == len(report["nodes"]), "Unmapped state has no line evidence")
        property_value(result, 0, 1, witness["projectSampleRate"])
        property_value(result, 0, 2, 1)
        track, item = -1, -1
        for object_id, obj in enumerate(result["objects"]):
            if obj["kind"] == 1:
                track += 1
                item = -1
                t = witness["tracks"][track]
                for field, key in ((3, "guid"), (4, "name"), (5, "gain"), (6, "pan"), (7, "channels")):
                    property_value(result, object_id, field, t[key])
            elif obj["kind"] == 2:
                item += 1
                i = witness["tracks"][track]["items"][item]
                for field, key in ((9, "position"), (10, "length"), (11, "fadeIn"), (12, "fadeOut"),
                                   (13, "name"), (14, "gain"), (15, "takeGain"), (16, "takePan")):
                    property_value(result, object_id, field, i[key])
                if not i["midi"]:
                    property_value(result, object_id, 17, i["sourceOffset"])
                property_value(result, object_id, 18, i["playRate"], 2)
                property_value(result, object_id, 19, i["pitch"], 2)
            elif obj["kind"] == 3:
                i = witness["tracks"][track]["items"][item]
                check(bytes.fromhex(obj["sourceTypeHex"]) == (b"MIDI" if i["midi"] else b"WAVE"), "Source kind lost")
                if not i["midi"]:
                    property_value(result, object_id, 20, "media/" + i["sourceFile"].rsplit("/", 1)[1])
        check(track + 1 == case["trackCount"], "Native track inventory changed")
        check(set(root.iterdir()) == {selected, saved} and selected.read_bytes() == source,
              "Worker/decoder resolved media or changed original files")
        # Reopen with selected source absent; no new worker or media dependency.
        selected.unlink()
        check(decoded(saved) == result, "Portable property inspection depends on absent original source")
        cases.append({"id": case["id"], "pid": child.pid, "sourceSha256": result["sha256"],
                      "objects": len(result["objects"]), "properties": len(result["properties"])})
        if case["id"] == "stereo-gain-pan":
            base_source, base_report, base_pid = source, report, child.pid
        saved.unlink()

    def mutate_property(index, field, value):
        def mutate(report):
            report["preview"]["properties"][index][field] = value
        return mutate

    fields = base_report["preview"]["properties"]
    gain = next(i for i, p in enumerate(fields) if p["id"] == 5)
    pan = next(i for i, p in enumerate(fields) if p["id"] == 6)
    name = next(i for i, p in enumerate(fields) if p["id"] == 4)
    rate_node = next(p["node"] for p in fields if p["id"] == 18)
    source_object = next(i for i, o in enumerate(base_report["preview"]["objects"]) if o["kind"] == 3)
    def swap_gain_pan(report):
        a, b = report["preview"]["properties"][gain], report["preview"]["properties"][pan]
        a["valueRange"], b["valueRange"] = b["valueRange"], a["valueRange"]
        a["number"], b["number"] = b["number"], a["number"]

    def disguise_valid_number(report):
        p = report["preview"]["properties"][gain]
        p.update(kind=0, status=4, reason=2, number=None)
        report["preview"]["lines"][p["node"]] = [4, 2]

    def disguise_unsupported_rate(report):
        p = next(p for p in report["preview"]["properties"] if p["id"] == 18)
        p.update(status=4, reason=8)

    changes = [
        swap_gain_pan, disguise_valid_number, disguise_unsupported_rate,
        mutate_property(gain, "object", 999999), mutate_property(gain, "id", 999999),
        mutate_property(gain, "kind", 99), mutate_property(gain, "status", 1),
        mutate_property(gain, "reason", 99), mutate_property(gain, "valueRange", [999999, 1]),
        mutate_property(gain, "number", 2), mutate_property(gain, "node", 0),
        mutate_property(name, "kind", 1), mutate_property(name, "number", 1),
        lambda r: r["preview"].update(schema=999),
        lambda r: r["preview"]["objects"][1].update(parent=1),
        lambda r: r["preview"]["objects"][1].update(kind=3),
        lambda r: r["preview"]["objects"][1].update(singleTake=False),
        lambda r: r["preview"]["objects"][source_object].update(sourceType=[0, 0]),
        lambda r: r["preview"]["objects"].pop(),
        lambda r: r["preview"]["properties"].pop(),
        lambda r: r["preview"]["properties"].append(copy.deepcopy(r["preview"]["properties"][gain])),
        lambda r: r["preview"]["lines"].pop(),
        lambda r: r["preview"]["lines"][0].__setitem__(0, 1),
        lambda r: r["preview"]["lines"][0].__setitem__(0, 3),
        lambda r: r["preview"]["lines"].__setitem__(rate_node, [0, 0]),
        lambda r: r["preview"]["lines"].__setitem__(rate_node, [4, 2]),
        lambda r: r["nodes"][1].update(keyRange=[0, 1]),
        lambda r: r.update(semanticStatus="converted"),
    ]
    saved = root / "corruption.scinspect"
    for change in changes:
        bad = copy.deepcopy(base_report)
        change(bad)
        bundle(saved, base_source, bad, base_pid)  # Recomputed checksum: test schema/ownership, not just hash failure.
        decoded(saved, False)
    saved.unlink()

    # The independent validator must also accept truthful refusals/ambiguity,
    # rather than only agreeing with the mapper on the native happy path.
    synthetic = [
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n VOLPAN -1 2 0 0 1\n NCHAN 1.5\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n NCHAN 2 4\n NAME "unterminated\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n VOLPAN 1 0 0 0 1\n VOLPAN 0.5 0.25 0 0 1\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n <ITEM\n NAME "ambiguous"\n VOLPAN 1 0 1 0\n >\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n <ITEM\n PLAYRATE nan 1 0 0 0 0\n <SOURCE WAVE\n FILE "missing.wav"\n >\n >\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n <ITEM\n TAKESEL 1\n SOFFS -0.5\n <SOURCE MIDI\n E 0 90 40 70\n >\n >\n >\n>\n',
        b'<REAPER_PROJECT 0.1 7.82 1\n <TRACK\n NAME `literal name`\n <ITEM\n FADEIN 1 0.05 0 0 0 0 0 0 0 0 0 0 0\n <SOURCE WAVE\n FILE \'missing.wav\'\n >\n >\n >\n>\n',
    ]
    for source in synthetic:
        selected = root / "synthetic.rpp"
        selected.write_bytes(source)
        child = subprocess.Popen([str(worker), "--rpp-properties", str(selected)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            output, error = child.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise AssertionError("Synthetic property worker exceeded bounded deadline")
        check(child.returncode == 0 and not error, "Synthetic evidence could not be inspected")
        bundle(saved, source, json.loads(output), child.pid)
        result = decoded(saved)
        check(result["propertiesVersion"] == 1 and result["retiredBytes"] == 0,
              "Truthful unsupported/malformed evidence failed to retire")
        selected.unlink()
        saved.unlink()

    # Bounded stdout refusal is terminal without a partial report.
    selected = root / "limit.rpp"
    selected.write_bytes(base_source)
    refusal = subprocess.run([str(worker), "--rpp-properties", str(selected), "--maximum-report-bytes", "1"],
                             capture_output=True, timeout=15)
    check(refusal.returncode != 0 and not refusal.stdout and b"import.resource_limit" in refusal.stderr,
          "Property sizing refusal published partial output")

print(json.dumps({"checks": checks, "platform": sys.platform, "cases": cases,
                  "realWorkerAndIndependentDecoder": True, "corruptChecksumRecomputedCases": len(changes),
                  "syntheticEvidenceCases": len(synthetic),
                  "semanticImportQualified": False, "renderComparisonQualified": False}))
