# Render-history portability between Linux and Windows

## Installed workflow found a real failure

Checkpoint 322 exercised the installed Linux Applications entry from source
`2580164` using an owned copy of the native Windows comp corpus. The test used a
private display, D-Bus session and PipeWire daemon with no physical endpoints.
No VM or user audio route was used.

Through the public accessibility controls, both selected clips were changed to
1.5× speed (4,000 output frames), positioned next to each other, and saved. The
retained-take crossfade dialog requested an equal-power transition at frames
13,968–14,032 with extended processed handles. **Save and render failed** before
applying audio. Saved clip state stayed unchanged, and request/job references
were retained. The installed workflow is not qualified.

A direct invocation of the same installed worker reproduced the failure. A
bounded debugger run identified the refusal in `verifyOwnedClipStretch`:
`Wrong native stretch memory metric`. The project contains genuine Windows-made
render history with `process-commit` metadata. Linux incorrectly required that
historical metadata to say `address-space`.

The test driver's later Ctrl+Q request also timed out after 30 seconds; its owned
app process was terminated during cleanup. That launch does not count as a
normal-Quit pass. App and private audio daemon are no longer running. The cause
of the shutdown failure remains to be checked separately.

## Correction

Completed stretch and comp-span artifacts now accept either supported producer
memory metric. This is historical producer metadata, not the current process's
memory enforcement policy. Unknown, missing, empty and wrongly typed metrics
remain invalid. No marker migration or rewriting is required.

Workers still enforce and report their own native memory limit. Existing grant,
request, geometry, source-media, audio-file, decoded-sample and peak checks remain
in force. The change does not equate address-space and process-commit measurements,
assert cross-platform DSP identity, or relax live worker limits.

Regression coverage accepts both metrics with identical verified edit/asset
identities and rejects altered sample hashes under each. It also rejects unknown
strings, null, numeric, boolean, array, object and missing metric declarations.
The worker harnesses separately check the actual producer's native metric.

## Validation and remaining work

A single low-priority Linux build and three focused suites passed:

- `comp-owned-span-worker`: 4.65 seconds.
- `stretch-worker-workflows`: 14.15 seconds.
- `protected-warp-worker-workflows`: 20.30 seconds.

These exercise actual isolated workers, owned floating-point media, checked
adoption and failure paths. They do not qualify a refreshed installed package.
The unchanged installed Linux and Windows candidates still contain the defect.

The original Windows-history request was then replayed in a separate project copy
with the fixed Linux worker, a fresh operation ID and its actual executable hash.
The recipe, historical metadata, media and worker limits were preserved. It
completed in 1.458 seconds and produced 50,671 stereo float frames at 48 kHz.
An independent RF64 read checked the file/sample hashes, geometry, finite samples
and peak `2.5156989097595215`. All 188 pre-existing project files remained
byte-identical. This is a worker regression check, not an installed UI pass.

The preceding CI [run 38748403745](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38748403745)
at `aa43e5e` completed successfully. Both separated Windows native test banks and
their required aggregate passed. This confirms the CI partition from checkpoint
321; it does not test the new portability fix. All 14 full job logs and 12 original
artifact archives were retained locally, with archive digests, sizes and CRCs
verified. No cancelled run was relabeled as successful.

The committed [evidence archive](../tests/results/M2/2026-10-15-portable-render-history/acceptance.zip)
contains the failed installed observations, owned project and original history,
debugger diagnosis, fixed-worker output, tests, source-file hashes and preceding
CI logs/metadata. Its adjacent manifest binds every archive byte. It includes no
application binaries, credentials or private session configuration. Large original
CI ZIPs remain local in `.cache/installed-short-comp-322/ci/`.

Next: refresh the candidate and complete installed Render/Review/Apply, Undo/Redo,
save/reopen/export and normal Quit. Independently verify the reverse platform
path. Full DAW, processing-quality, localization and hardware gates remain open.
