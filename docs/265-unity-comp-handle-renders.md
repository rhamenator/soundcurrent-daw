# Unity comp handle generation and atomic admission

Checkpoint 265 generates actual missing handle buffers through the existing
isolated worker, reviews them and admits both fragments as one structural edit.
The first producer supports exact unity maps and zero independent pitch. General
stretch/pitch, broader warp/context generation and UI supervision remain required.

## Model and review

`ApplyCompHandlePair` stages optional replacements for both fragments, then extends
the geometry and applies the paired fade. Expected clips and current/raw/nominal
asset snapshots must match. Original nominal anchors remain unchanged; the new
buffer preserves existing prefix/suffix coverage and adjusts only its playback
offset. The existing project transaction provides rollback if the second fragment
fails. Undo/Redo includes both assets and clips in one change. Ordinary
`ApplyClipStretch` retains its separate raw-anchor guard.

`prepareUnityCompHandleRender` requires unity interval maps and zero independent
pitch. It prepares a whole-buffer unity-copy request for the existing v4 worker;
it does not reinterpret a nonunity interval as a different playback speed.
The original nominal state remains separate from this processing request.

The child result must first pass the existing owned completion-marker, raw input,
full RF64/sample/hash and policy verifier. `verifyUnityCompHandlePair` then checks
current model/revision, pins and hashes source/current/nominal files, and streams
candidate samples through bounded slabs. Its current and nominal inner regions
must have identical float PCM hashes to the original buffers. It rechecks source
bytes and current expected state before producing a private verified edit owner.
No file publication or project mutation occurs during this review.

All planning, hashing, decoding, allocation and resource retirement are off audio.
The child decoder's allocations/time remain cooperatively bounded as described by
the existing wave verifier; these APIs are not an OS privilege sandbox.

## Actual acceptance

[Source-bound evidence](../tests/results/M2/2026-10-13-unity-comp-handle-renders/index.json)
records source `500d0d4e0b97f71a86f896dba5d0a97d9ea1c134` and eight selected
Linux gates passing in 1.43 seconds. The worker bank contains 16 actual renders and
worker PIDs: eight original nominal buffers plus eight extended buffers across
mono, stereo, fractional-origin stereo and four discrete channels.

Each scenario performs owned review, malformed-second-fragment rollback, stale
revision/missing-artifact/cancellation/changed-byte refusal, one-step paired
Undo/Redo, real project save/reopen, and all three fade curves. Live playback with
127- and 511-frame partitions equals 37-frame offline exports. Callback guarded
allocation/free/blocking-lock counts are zero. Each C++ scenario has 61 checks.

The integer-origin scenarios compare nine full PCM results against independent
raw waveform/fade expectations. The fractional-origin case has original/current
PCM preservation and live/export comparisons, not an independent fractional
kernel oracle. Original raw WAV bytes remain unchanged. The public archive
contains owned raw/derived/export PCM, requests, completion markers and logs.
These synthetic software cases are not physical endpoint or full sonic-quality
qualification.

The first fixture wrote workers into a jobs directory different from the verifier's
owned `media/derived` destination and was correctly refused as missing media.
Its original logs/requests/receipts are retained; the corrected fixture passes.
Initial compiler indentation warnings were corrected. No unchanged failed run is
used as qualification.

The preceding window-state revision passes all four jobs in
[run 38488371125](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38488371125):
92 native Windows, 22 desktop Windows and 167 Linux suites. Original terminal
APIs/logs/five ZIPs are retained; independent byte/source audit verifies 14
payload files, 1,519 corresponding source files and both reviewed media DLLs.
Native main-window, physical audio and installer flags remain false.

This implementation is under [run 38491971110](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38491971110)
at this checkpoint. Retain its original terminal evidence before branch updates.

## Next work

Implement nonunity/pitched and general protected/context producers with explicit
fractional endpoint versus integer-grid contracts and PCM/phase/edge quality
checks. Add paired stretch regeneration, controller revision/approved-root binding,
UI supervision, cancellation/project-close and recovery. Extend acceptance to
previously extended buffers, processed retained takes, mixed raw/rendered pairs,
linked rates and broader layouts. Review all retained-file dependencies during
that extension. Integer base PCM preservation alone is insufficient to qualify
SRC neighborhoods after new samples are added.

Installed current/previous Ubuntu previews and equalizer repositories are unchanged.
Full-suite, Windows installed delivery and European language qualification remain open.
