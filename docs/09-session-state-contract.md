# Implemented session-state contract (S1/S2 foundation)

The root CMake build exposes Qt-free `SoundCurrent::Session` and `SoundCurrent::ProjectStore`. These two state components contain no playback or capture callbacks. Their methods allocate and run only on the control/disk side; they are **not real-time APIs**.

## Schema v1.8 (with explicit v1.0–v1.7 migration)

[encodeProject](../src/project_store.cpp) defines the exact JSON fields; tests exercise inverse decoding. `project.json` stores session/track/processor/band/clip/asset UUIDs, names, sample rate, playhead/export frames, layout, backend/port intent, EQ enable/frequency/gain/Q and relative hashed media. No machine-specific numeric device ID is persisted.

Canonical lowercase UUIDs identify objects; parameter addresses use track + processor + band UUID and fixed descriptor IDs. Track names and band order can change without changing parameter addresses. One drag gesture produces one undo item; cancel restores its initial value. Scalar EQ gestures, input/output/monitor route edits and recording-monitor preferences share a configurable admitted history (256 commands by default); history is not serialized. Schema 1.1 adds strict per-channel descriptors and a separate monitor intent. Schema 1.2 adds stable Off/Post-EQ `monitoringMode`. The decoder explicitly migrates v1.0/v1.1, defaulting monitoring Off and preserving opaque legacy route strings; the writer emits 1.8. See [routing](26-project-routing.md) for matching, limits and named placeholders. See [monitoring preferences](27-monitoring-preferences.md) for passive restore and accepted-prefix preparation. Schema 1.3 adds the saved master layout/matrix/output intent; 1.4 adds desired project-frame punch settings, disabled by default for older projects. See [master matrix](33-master-matrix.md) and [project punch controls](54-project-punch-controls.md). Schema 1.5 adds strict per-track whole-frame `inputLatencyFrames`, defaulting to zero for v1.0–v1.4. [Input latency controls](60-input-latency-controls.md) freeze this declaration at preparation; restore never activates audio. Schema 1.6 adds explicit `auto-recording` monitoring without changing older Off/Post-EQ preferences; older schemas reject the new identifier. See [Auto monitoring](61-auto-recording-monitoring.md). Structural track/clip/master/punch/input-delay edits share the bounded transactional history. General tempo, plugin and loop/take state remain future schema work.

Trusted default grants are 32 MiB project text, 256 MiB parser staging and 64 MiB canonical state; JSON nesting depth is32. Track, asset and clip counts are admitted by configured payload budgets, rather than fixed product ceilings. Current EQ/layout envelopes remain64 bands per track and1–256 channels; text fields remain4096-byte UTF-8. See [resource-admitted tracks](78-resource-admitted-projects.md) for implementation and explicit remaining scalability gates. Frames are nonnegative int64; overflow and float-to-frame conversion are rejected. IDs must be globally unique. Unknown fields, processor versions or schema versions fail explicitly rather than disappear during load/save. A later migration must be explicit and tested before accepting a newer schema.

## Media and paths

Media remains under `media/` using portable UTF-8 relative paths. Reject traversal, absolute/drive paths, symlinks/reparse points, NUL/control characters and reserved Windows device names. Verify SHA-256 before loading or saving a referenced asset; a same-named different file is an error. Moving the entire directory preserves links.

The original state fixtures use a three-byte hash fixture; S4 additionally saves/reopens/moves actual synthetic RF64 recordings with header/sample/prefix checks. General media import, missing-media relink UI, cross-rate resampling, Unicode normalization/case-collision detection, and long-path deployment qualification remain open. Sample rates in the state model do not imply an implemented resampler. S3/S4 must reject unsupported media playback formats until their adapters exist.

The project directory and its ancestors must be owner-controlled. Path component checks are not a sandbox against a hostile process replacing directories during I/O. Symlink checks and a cooperative writer lock do not establish protection against malicious concurrent filesystem mutation. Broader untrusted-project/plugin containment is a separate architecture task.

## Save/recovery

Acquire a per-project OS writer lock (`flock` on Linux, exclusive Win32 handle on Windows). The lock file stays present; OS lock ownership disappears on process exit. Other writers cooperating with this API cannot publish simultaneously. Encode/validate and verify media first. Refuse overwriting an unrelated project identity or malformed current project.

Before replacing the current generation, publish its validated JSON as `project.previous.json`. Flush a uniquely owned same-directory temporary file, invoke a cancellation boundary, then publish it. Linux uses `fsync` + `rename` + directory `fsync`; return the achieved durability level. Windows uses `FlushFileBuffers` + `MoveFileExW` with replace/write-through, and reports file-flushed only. Microsoft documents [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers); NTFS crash/power-loss behavior still needs native fault qualification. Filesystem/hardware guarantees vary.

Cancellation before publication leaves the current project unchanged and removes only this save's temporary file. Recovery from `project.previous.json` is explicit (`loadPrevious`), never an invisible substitution. A directory-flush failure after publication reports reduced durability, not a false rollback. This is a snapshot foundation, separate from the S4 versioned checkpoint journal and verified-prefix copy recovery in [recording contract](13-recording-contract.md). No append-only edit/event log is implemented yet.

Remaining fault gates include physical disk-full/permission/I/O failure, kill/power-loss on actual filesystems, and native Windows save/lock/path runtime tests. Current cancellation/nested-writer tests do not substitute for those results.

`ProjectStore::verifyMedia` is now a public control/I/O-side API used by verified recorded-take admission. It validates the session and owned root/path components before hashing. This adds no schema fields or real-time calls; see [S6f attachment](21-native-recording-owner.md).

## Schema1.8: portable original import evidence

The writer now emits1.8. Older1.0–1.7 states explicitly default to an empty import
list; older schemas reject the new key. Generated source/operation IDs and exact
relative `imports/` paths bind original byte counts/hashes, immutable inspection
bundles and verified per-asset occurrence receipts. Save/reopen verifies complete
source/report/receipt/owned-audio agreement before returning or publishing state.
Original roots never grant historical access. Imported-source/asset indices avoid
repeated whole-project validation per archive. [Checkpoint128](128-portable-import-project-evidence.md)
details grants, cancellation/residue, migration tests and semantic conversion gaps.
Original properties retain source units/status; gain/fade/rate/pitch/timing mapping
and full native compatibility remain unqualified.
