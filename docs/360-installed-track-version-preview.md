# Installed Linux preview: waveforms and grouped track versions

Checkpoint 360 delivers the current audio-version and waveform work through the
local **SoundCurrent DAW Preview** Applications entry. It remains an experimental
preview. Full-suite parity, processing quality, bundled content, foreign-project
compatibility and all-Europe localization are still incomplete.

## Delivered candidate

- Linux source: `931289108e7396979264320c2b6f9040ff7aa364`.
- Version: `0.1.0~preview.20261016013412.931289108e73`.
- Separate per-user installation; the system package database was not changed.
- The normal Applications launcher points to this candidate after qualification.
  **SoundCurrent DAW Preview (before track versions)** preserves the previous
  `20261015223130.f2f9f7c8916e` installation for rollback.
- Release DEB and exact corresponding-source archive are prepared locally. No
  binary release was uploaded, no PR was merged, and no physical audio or VM was
  used in this checkpoint. The existing Windows preview remains unchanged.

The source archive contains all 8,199 tracked files: 8,193 match raw Git blobs and
six match the repository's declared text filters. Full membership, archive bytes,
SHA-256 and gzip CRC were verified. Its SHA-256 is
`8772acbfb7f5bdc9d8549ebf84c469bbd587039715daa6ece41e0380756828e8`.

## Qualification

The actual app, helpers and selected test executables were rebuilt from the clean
frozen revision, with one compiler worker and idle I/O priority. All **35 selected
Release tests pass in 185.80 seconds**, including native owned recording,
localization/catalog rejection, waveform worker/protocol/cache/controller/UI,
track versions, project state, media provenance, independent export, stretch and
protected-worker workflows, comp editing and package provenance. Source and
binary hashes were unchanged throughout that qualification. This is a selected
preview gate, not a claim that every possible workflow is qualified.

The extracted, byte-verified package was copied to its separate user installation.
Actual installed UI testing used public accessible controls on a private X display
and a private PipeWire daemon containing only Dummy/Freewheel nodes. It did not
connect to the user's speakers, microphones or live music routes.

Five normal application launches and exits verify:

1. Waveform source-channel selection, explicit absent-channel display, unavailable
   owned source detection, media restoration and Retry. Project state and Undo
   availability stay unchanged.
2. Two speed-edited clips, rendered retained-handle crossfade, Review/Apply,
   Undo/Redo, save and normal Quit. Rendering completes in **4,380 ms** against the
   original 30-second bound; the elapsed indicator stops at completion. Both
   observed workers match the installed payload and retire normally.
3. Reopen and export of 18,032 stereo frames at 48 kHz. The float32 WAV matches an
   independent mixing/gain/fade calculation **bit for bit**, with zero maximum
   error. Its peak of 5.5167 preserves floating-point headroom.
4. A separate owned two-track fixture: duplicate a shared alternative named
   `Alternative été — Київ`, mute one alternative clip, activate both originals
   through **Select matching tracks**, then exact Undo/Redo and save/reopen. Group
   identities, original arrangements, alternative edits, Unicode names, routing,
   processing settings and immutable media survive. This installed workflow does
   not establish every track type or 129-track GUI performance; the larger core
   group evidence remains in checkpoint 359.
5. The normal Applications launcher opens the exact new installed executable and
   saved project, then exits through public Quit. All five app processes and their
   private audio daemons retire. Installed payloads and the old preview remain
   byte-identical to their verified inventories.

Original observer failures are retained. A fixed 0.5-second save read raced the
asynchronous save; subsequent observation confirmed the intended file without
repeating edits. Save observers now wait for publication and public Ready under a
10-second bound. An early render observer ran before its dialog existed and
failed before launching work. A checkable control's accessible role and an
immediate selection observer were corrected from the actual accessible tree; a
non-clip verifier initially removed clips on only one comparison side. The same
live workflows continued. Product code and the actual render deadline were not
changed to accommodate these observer failures.

## Windows CI follow-up

Original run `38833954553` on the delivered Linux source completed with all Linux
jobs successful. The Windows native recording job passed 95 of its 96 tests, but
`retained-take-comp` threw a Windows code-page conversion error. Its aggregate gate
also failed. Other Windows jobs passed. All **15 original terminal job logs** and
**12 original artifact ZIPs** were retained before any PR advancement, with API
sizes/digests and ZIP CRCs verified. This run remains recorded as failed.

The new test's missing-media fixture used `media.string()` on a path containing
Ukrainian text. The test-only correction keeps a native `std::filesystem::path`
and appends the ASCII holding suffix directly. It preserves the Unicode fixture
and all assertions. Correction commit:
`5c871ec1191d313ca3ef7aeb983dd76a93e7586c`. The targeted Linux retained-take/comp suite
passes; a fresh native Windows run must confirm the correction. The shipped Linux
product is unchanged by this test repair.

## Evidence and next work

[Acceptance archive](../tests/results/M2/2026-10-16-installed-track-version-preview/acceptance.zip):
1,939 entries, 7,497,010 bytes, SHA-256
`1f6c9265ff2b27b4cf523ae3bbfe87df3e44ae6867d5630011b4df5a5badb5e1`.
The archive contains original build/test logs, source and executable identities,
packaging receipts, installed UI scripts/captures/state, observer failure logs,
independent export results, the test correction, and all original CI job logs with
artifact identities. Original CI ZIPs and generated media remain local; binaries,
private preferences and transient bus addresses are excluded. Archive CRC and all
payload entry hashes were verified.

Next: qualify the corrected native Windows test and deliver the corresponding
Windows functionality through the existing brief independent-VM workflow. Keep
feature development frozen while completing that delivery; do not continually
move the preview target. Clean-machine installation, hardware/driver diversity,
sustained recording/performance, broader track-version workflows and the full
M0–M11/F/Q/C/N/X004/X005/all-Europe scope remain open.
