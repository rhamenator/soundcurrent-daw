# Implemented session-state contract (S1/S2 foundation)

The root CMake build exposes Qt-free `SoundCurrent::Session` and `SoundCurrent::ProjectStore`. It has no playback or capture callbacks. All current methods allocate and run only on the control/disk side; they are **not real-time APIs**.

## Schema v1.0

[encodeProject](../src/project_store.cpp) defines the exact JSON fields; tests exercise inverse decoding. `project.json` stores session/track/processor/band/clip/asset UUIDs, names, sample rate, playhead/export frames, layout, backend/port intent, EQ enable/frequency/gain/Q and relative hashed media. No machine-specific numeric device ID is persisted.

Canonical lowercase UUIDs identify objects; parameter addresses use track + processor + band UUID and fixed descriptor IDs. Track names and band order can change without changing parameter addresses. One drag gesture produces one undo item (up to 256 retained gestures); cancel restores its initial value. Undo state is presently in-memory, limited to EQ scalar gestures, and not serialized. Recording/monitor/latency state and general edit commands are future schema work.

Input limits: 4 MiB project text, JSON nesting depth 32, 256 tracks, 4096 assets, 8192 clips total, 64 EQ bands per track, 1–256 layout channels and 4096-byte UTF-8 text fields. Frames are nonnegative int64; overflow and float-to-frame conversion are rejected. IDs must be globally unique. Unknown fields, processor versions or schema versions fail explicitly rather than disappear during load/save. A later migration must be explicit and tested before accepting a newer schema.

## Media and paths

Media remains under `media/` using portable UTF-8 relative paths. Reject traversal, absolute/drive paths, symlinks/reparse points, NUL/control characters and reserved Windows device names. Verify SHA-256 before loading or saving a referenced asset; a same-named different file is an error. Moving the entire directory preserves links.

Current tests use a three-byte hash fixture, **not a recorded WAV**. WAV header/rate/frame validation, import, missing-media relink UI, cross-rate resampling, Unicode normalization/case-collision detection, and long-path deployment qualification remain open. Sample rates in the state model do not imply an implemented resampler. S3/S4 must reject unsupported media playback formats until their adapters exist.

The project directory and its ancestors must be owner-controlled. Path component checks are not a sandbox against a hostile process replacing directories during I/O. Symlink checks and a cooperative writer lock do not establish protection against malicious concurrent filesystem mutation. Broader untrusted-project/plugin containment is a separate architecture task.

## Save/recovery

Acquire a per-project OS writer lock (`flock` on Linux, exclusive Win32 handle on Windows). The lock file stays present; OS lock ownership disappears on process exit. Other writers cooperating with this API cannot publish simultaneously. Encode/validate and verify media first. Refuse overwriting an unrelated project identity or malformed current project.

Before replacing the current generation, publish its validated JSON as `project.previous.json`. Flush a uniquely owned same-directory temporary file, invoke a cancellation boundary, then publish it. Linux uses `fsync` + `rename` + directory `fsync`; return the achieved durability level. Windows uses `FlushFileBuffers` + `MoveFileExW` with replace/write-through, and reports file-flushed only. Microsoft documents [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers); NTFS crash/power-loss behavior still needs native fault qualification. Filesystem/hardware guarantees vary.

Cancellation before publication leaves the current project unchanged and removes only this save's temporary file. Recovery from `project.previous.json` is explicit (`loadPrevious`), never an invisible substitution. A directory-flush failure after publication reports reduced durability, not a false rollback. This is a snapshot foundation, not the append-only interrupted-recording journal planned for S4.

Remaining fault gates include physical disk-full/permission/I/O failure, kill/power-loss on actual filesystems, and native Windows save/lock/path runtime tests. Current cancellation/nested-writer tests do not substitute for those results.
