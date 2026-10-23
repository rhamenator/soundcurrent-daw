# Final comp playback source preparation

Checkpoint 284, 2026-10-14. The complete frozen DAW goal remains active.

## Implemented preparation

`prepareCompPlaybackSource` derives an immutable, memory-accounted source
replacement from the current paired processing plan. It keeps the exact original
clip, raw/base/nominal asset declarations and raw timing map separately from the
proposed playback geometry. No project edit or filesystem operation occurs.

For a physical artifact, playback preserves the original rational speed and
addresses the expanded physical source. For project-after-SRC audio, playback
addresses the real project-rate output at unit speed and zero source offset.
This avoids applying the original conversion a second time. Gain, polarity,
mute and fade anchors retain their alignment when the fragment extends backward.
The original clip ID and retained-take reference survive preparation.

The new artifact does not inherit a stretch anchor or same-rate handle window
that describes the old nominal asset. Those values remain in the original
snapshot. An exact affine transform followed by the retained piecewise map
resolves final artifact positions back to raw source positions, including
fractional crops and nonuniform protected timing. This does not implement a
nonuniform audio producer.

Preparation rechecks the live plan context and original source declarations. It
rejects missing/unexpected output assets, reused asset identities, invalid paths
or hashes, mismatched rate/layout/frame geometry, unrelated capture evidence and
insufficient resource credit. Its output is metadata, not file verification,
sound-quality admission or authority to apply audio.

## Evidence

Source `b6a62b52119215765598b836db11ded8f8394003` passes the expanded processing
permission suite. CTest process 3942790 retires successfully; test time is 0.44
seconds, enclosing qualification time 0.53 seconds. Production source, the test
executable and local static libraries have identical before/after hashes.
See the [evidence index](../tests/results/M2/2026-10-14-comp-playback-source/index.json).

- 110 prepared sources cover 44.1/48/96 kHz sources, unit and 3/2 speed,
  fractional origins, two transition widths, offset source windows, existing
  media, protected timing maps and prior expanded handle artifacts.
- Every original audible sample maps to the same raw position after preparation.
  Fractional midpoint positions and extended endpoints retain exact rational
  timing; the complete original source metadata is preserved.
- Nondefault gain, polarity and fade windows keep their original alignment.
  Post-SRC playback uses the new artifact's actual sample rate and unit speed.
- Invalid geometry, hash, path, identity, missing/unexpected output, stale owner,
  invalid side, out-of-range/faulty rational position, moved object and quota
  refusal are covered. Resource reservations return to their previous value.
- The existing outside-transition PCM isolation checks still pass: 51 physical
  and 35 post-SRC generated cases. Total assertions: 93,184.

This corpus uses synthetic metadata and PCM. It does not publish a new assembly,
exercise native audio, qualify Windows execution or measure sound quality. The
initial build failure from a missing direct timing-header include is retained;
the include was fixed and the subsequent build emitted no compiler warnings.

## Required integration

The proposed playback clip is **not independently canonical**. Generated clips
retain their take identity while their original stretch metadata now lives in
the prepared provenance record. Do not drop that identity to make existing
validation pass. Canonical state must represent and validate both together.

Next add the versioned persistent provenance record and atomic paired edit that
consumes output reservations, installs actual final assets and preserves the
original snapshots. Include payload/history accounting and conflict-safe
Undo/Redo. Validate existing-media extensions as well as generated fragments;
existing nominal context exposure cannot be made valid merely by extending the
clip range. Repeated edits and regeneration must use retained raw provenance,
not silently treat processed media as a new recording.

Then exercise actual saved/reopened playback and WAV export through the shared
reader, followed by main-window integration. The current prepared record is not
yet serialized, adopted, connected to the main window or installed. The earlier
controller CI run remains active at this checkpoint; the PR head is preserved.
Nonuniform/protected production, sound-quality findings, native installation,
localization and the full remaining parity backlog remain required.
