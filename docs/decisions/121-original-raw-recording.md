# ADR121: direct persistence of the original stamped raw route

Status: original writer and streaming prefix verifier implemented; native/project
composition and Windows execution open. Date: 2026-10-11.

## Decision

Consume leased original raw slabs on the disk owner. Use original GPL-3.0-only
C++20 code and existing libsndfile RF64 float storage, OpenSSL SHA-256, nlohmann
JSON, secure media I/O and resource leases. No dependency, driver or license is
added. The existing dependency inventory and notices apply.

Persist fixed 80-byte original packet records, independent audio/record hashes
and a strictly versioned common-prefix journal. Flush both extents before
publishing the journal. Inspection is paged and inactive-only. Preserve original
float evidence, including nonfinite payloads, while recording a separately
verified finite prefix. Do not weaken existing playable-media validation.

| Alternative | Assessment |
|---|---|
| Feed the old CapturePipe/CaptureWriter | Sanitizes exceptional samples and uses signed engine-frame timing |
| Convert native positions or QPC through floating point | Loses exact large integers and conflates timing domains |
| Keep all packet metadata in an in-memory array | Memory grows with recording duration |
| Publish audio and metadata independently | Recovery cannot establish a synchronized committed prefix |
| Attach every original job as playable media | Admits nonfinite evidence and fabricates missing alignment |

Ownership transfers only before threads start or after they join. Monitor loss
can leave an additional intact raw packet; retain that real disposition rather
than trim the raw route to the monitor extent. Recording alignment requires a
separate original-clock mapping, not monitor output ordinals.

[Checkpoint189](../189-original-raw-recording.md) records process interruption,
three-owner disk concurrency, race/sanitizer and corruption acceptance. Windows,
power-loss scope, recovery copying and native/project workflow gates stay explicit.
