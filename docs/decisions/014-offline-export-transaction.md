# ADR 014: private shared-engine export with completed-file publication

Status: accepted for S7a, 2026-10-05; desktop/render-quality/platform gates remain open.

Use the existing `TrackReader`/`PlaybackPipe` and a private `PreparedEq`, rather than
maintaining a separate export DSP implementation. Preroll from the earliest clip to
preserve selected-range state; accept the current zero-latency descriptor explicitly.
Future automation, delayed graph paths and additional processors need their own
render/PDC/tail contract. Float WAV keeps graph headroom; PCM/dither/resampling are not
adopted without their format/quality fixtures.

Run a synchronous cancellable transaction on the I/O owner, later wrapped by a separate
desktop worker. Hash reads gain an optional per-chunk cancellation callback without
changing existing default call sites; read-ahead has a separate admission hook so its
existing per-timeline-read behavior remains stable. No new library is introduced.

Publish only a finalized/flushed complete sibling temporary. Default publication is
exclusive; replacing a current file requires an approved content hash and revalidation.
Protect in-project snapshots/media and hard-link replacement aliases. Retain the owned
immutable filesystem assumption explicitly: final hash followed by rename is not hostile
writer compare-and-swap. Report a postpublication directory/cleanup failure as a complete
file with reduced durability, never as a cancellable or unpublished partial result.

Document actual Linux debug/sanitizer, independent parser/oracle, signal cancellation
and headless Windows cross-build evidence separately. Do not claim native Windows,
physical disk-full/power-loss, >4 GiB completion, automatic crash-temp recovery or S7/S8
completion from the core API and CLI.
