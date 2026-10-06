# S7a: shared-engine offline float WAV export

Status: **worker API and developer CLI implemented, 2026-10-05**. This is the export
core; [S7b desktop binding](24-desktop-export.md) now exists. Complete SLICE-001 and DAW parity remain unproven.
Previous goal checkpoint was progress: `df920a5` adopted the equipment catalog update.

## Current workflow

The opt-in media build produces `sc-export-tool` without Qt or an audio backend:

```sh
.cache/build-desktop/sc-export-tool render PROJECT_DIRECTORY OUTPUT.wav
.cache/build-desktop/sc-export-tool render PROJECT_DIRECTORY OUTPUT.wav --start 137 --end 12003
.cache/build-desktop/sc-export-tool render PROJECT_DIRECTORY OUTPUT.wav --tail
.cache/build-desktop/sc-export-tool fingerprint EXISTING.wav
```

The CLI reopens the saved immutable project and exports the first track. It uses a
nonempty saved export range, or the full first-track clip extent when that range is
empty. It does not export current unsaved GUI edits. Frame overrides are half-open,
nonnegative timeline coordinates. The API selects a stable track ID and explicit
range. Destinations are caller supplied; their parent directory must exist. Inside
this project only `exports/` is admitted, protecting snapshots and raw media. External
replacement targets with hard-link aliases are refused. Plain directory/file and
Windows reparse checks supplement the existing owned-project filesystem contract.

Existing files are refused by default. An explicit replacement request supplies the
SHA-256 of the approved current file with `--replace-sha256 CONFIRMED_HASH`. The core
checks it before preparation and immediately before publication, refusing a changed
or missing target. A caller must obtain real user approval for that file/content;
automatically fingerprinting and replacing it is not confirmation. The desktop
presents a worker-inspected, explicit destination/overwrite choice in S7b. This is content revalidation under
an owned-filesystem contract, not an atomic compare-and-swap against hostile writers
between the final hash and rename. General adversarial filesystem containment remains
an independent security gate.

## Processing and timing

`exportTrackWav` is synchronous on its I/O owner. It uses the existing bounded
`TrackReader`/`PlaybackPipe` to decode and sum source-offset clips and gaps, then a
**private `PreparedEq` instance from `sc-engine`**. No live graph, audio device, GUI,
equipment library or canonical model is changed. Reader I/O, hashing, buffers, file
creation and publication stay outside realtime callbacks. The calling desktop must
dispatch the complete job to a worker; S7b now connects an independent job owner to the window.

Preparation checks the existing session/channel/rate/EQ/media contracts. Matching
source rates are required; resampling is unimplemented. The source handles are
read-only. The renderer verifies hashes at admission and verifies the project media
again before publication, catching accidental changes during the job. Concurrent
hostile/ABA source mutations are outside the current immutable-media ownership model.
Nonfinite source samples or processor numeric faults fail the job instead of quietly
publishing a sanitized take. Invalid or missing unused project media also blocks the
final verification; missing-media placeholder/render policies remain later work.

The graph starts at the earliest relevant clip (or the selection start if there are
none), feeding all preceding timeline samples into the EQ before writing the selection.
Leading silence before that first clip has zero state and may be skipped. This preserves
filter history rather than resetting EQ at a selected start. Processing uses the same
coefficients and sample path as live playback. The current processor descriptor has
zero latency; the adapter explicitly requires it. Future delayed graph support needs
latency compensation rather than silently shifting the export. Static snapshot EQ is
implemented; automation/event graphs remain M3 work.

The default ends exactly at the selection end. Optional `UntilSilent` tail processing
feeds zeros after that end, requiring at least 100 ms of consecutive samples within
all-channel amplitude threshold (default `1e-6`, −120 dBFS). A declared frame cap bounds
work; reaching it before that hold reports `tailTruncated=true`. The cap can be smaller
than the hold deliberately, yielding a disclosed truncation. This is a magnitude/hold
policy, not proof of mathematically infinite IIR silence or other effect-tail quality.
No extra source samples beyond the selected end enter the tail.

Float32 output retains headroom above ±1. The report includes written peak and count
of samples above full scale; it neither normalizes nor clips them. No PCM24, dither,
resampling, mastering protection or loudness normalization is claimed. RIFF/WAVE float
is used when the declared maximum extent fits a conservative 32-bit size bound; RF64
is selected for larger extents or `--rf64`. Multichannel synthetic tests preserve
channel order/count as discrete channels; named speaker masks, BWF metadata, surround
semantics and physical interfaces remain separate requirements.

## Bounded jobs and publication

Audio-buffer admission includes the 32-slab pipe, the reader's float/double scratch
and three renderer scratch buffers. The default audio budget is 128 MiB, within the
existing 256 MiB admission cap; model/binding/processor state has its existing separate
count bounds. The current export open-source budget is 64 files. The explicit processing-duration
budget includes preroll and the maximum tail (default CLI 24 hours at project rate).
Exceeding admission fails visibly. None of these are measured realtime deadline or
whole-process resident-memory guarantees.

Cancellation is checked during source/output/confirmation hash reads (64 KiB chunks),
at preparation/processing/publication boundaries and between blocks. Blocking OS I/O
must return before a cooperative check can run. SIGINT/SIGTERM request cancellation
in the developer CLI. No device/player process is stopped. The job creates one unique
exclusive `.partial` in the destination directory. Its owner closes/removes it after
cooperative cancellation or a prepublication error, preserving existing destinations.

Only after complete WAV header finalization, file flush, hashes, source revalidation
and final approval/cancellation checks does it publish. Linux no-overwrite publication
uses hard-link creation; confirmed replacement uses atomic rename. Windows uses wide
`MoveFileExW` with replacement enabled only with the explicit token. The directory is
flushed where supported. Cancellation after publication does not undo a completed file.
A postpublication directory flush/temporary-alias cleanup error returns a complete
published file with `publicationWarning` and weaker declared durability. It does not
misreport that file as a failed partial export. A normal no-error Linux result declares
file and directory flush; Windows currently declares file flush only.

Process kill/power loss may leave an unpublished temporary file; automatic discovery
and cleanup for interrupted exports remain open. Physical disk-full, forced OS close/
flush/syscall failure, hostile publication races, >4 GiB completion and native Windows
runtime/durability still need independent qualification. Boundary-injected failures
prove the owner choreography, not those physical outcomes.

## Evidence and next task

[Manifest](../tests/results/SLICE-001/2026-10-05-offline-export-core.json).
Linux debug and ASan/UBSan/LSan: all 14 CTest groups pass, including 937 export fixture
checks. Tests compare exact selected output against the guarded live driver with
blocks 16/64/127/512/2048; channel counts 1/2/8/32/256; source offsets/overlaps/gaps,
preroll, EQ disabled, finite tails and disclosed caps. New tests exercise hash/extent/
resource admission, nonfinite media, destination consent/change/collision, failures and
cooperative cancellation at four stages, source mutation, protected paths and Linux
symlink/hard-link refusal. The guarded live DSP path reports zero host allocation,
free or blocking-lock calls; the disk/render supervisor itself is not a realtime path.

`tests/verify_export_cli.py` records a real owned synthetic raw take, reopens an edited
fixture with the native state decoder, independently parses RIFF/RF64 chunks/float GUID/
extents, evaluates a direct-form-I peaking oracle (the engine uses transposed direct
form II), and checks every selected sample/hash. It preserves project/media bytes,
refuses unconfirmed overwrite, re-encodes an approved replacement to RF64 with exact
sample preservation, and cancels a genuine large RF64 writer using SIGTERM. No physical
audio or native Windows execution is involved. The Windows headless export/core/tests
cross-build passes; it is not a runtime or Qt qualification.

**S7b binding now exists:** [desktop snapshot export](24-desktop-export.md) captures
the immutable accepted session/revision, exposes track/range/tail/destination and
worker-inspected overwrite consent, preserves live transport and joins at close.
The next task is S8 and remaining recording/native/routing/platform acceptance.
All 92 frozen parity rows, X004/X005 processing/portability and localization remain open.
