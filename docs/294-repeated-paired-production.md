# Repeated paired comp production and verified adoption

Checkpoint 294, 2026-10-14. Full DAW goal remains active.

Paired rendering now reviews original source histories before publication and
again before committing completion. Public verification reviews both envelopes'
histories around full file and PCM validation. An unchanged assembled sibling is
still a dependency. The iterative history reviewer uses a private verifier for
one original assembly, preventing recursive ancestry traversal.

Regenerated sources use their original nominal clip and stretch settings when
checking nominal media. Handle planning also recognizes retained assembled takes
and converts their allowed coverage through the recorded raw map. Requests beyond
that retained coverage remain invalid. See [ADR 181](decisions/181-repeated-pair-history-boundaries.md).

## Actual workflow corpus

The new corpus starts with real, rendered and adopted source histories at 44.1,
48 and 96 kHz. At each source rate it performs two successive paired edits, then
a mixed edit that preserves an assembled take in a retained lane, uses a cropped
active copy as the unchanged sibling, and renders the other side from its
original nominal source. These additional generations have actual owned jobs,
completed audio and canonical adoptions.

Each generation checks historical failure before job creation, changed history
during later verification, stale metadata at queued Apply, Undo/Redo, saving and
reopening, and live core playback against the complete exported WAV. A separate
case changes original history after the final publication callback and must leave
no completion marker. Every original marker is restored by the fixture.

The full assembly verifier compares complete output PCM and applies the existing
media-scope reviewer. An additional before/after playback comparison checks the
outer timeline regions, excluding the transition and an explicit 128-frame guard
on either side. That additional comparison is not a claim about the excluded
edge samples. The corpus uses the shared renderer; it is not an independent
sound-quality reference.

The initial development run passed nine adoptions and fifteen child workers,
with 502,422 unchanged-region samples and 508,182 live/export samples. Its first
attempt had a fixture error: the exported WAV was passed to the project-media
cache, which correctly rejects paths outside `media/`. The fixture now reads
the export through an owned descriptor and libsndfile. Failed inputs and logs
are retained. Source-bound qualification also includes an explicit refusal for
handles beyond the retained assembled take's coverage.

## Source-bound qualification

The final source qualification at `512ad9273c81a6a94fc60d741c00373003a4ac0a`
passes all ten selected suites in 401.31 seconds. Source and binary hashes remain
unchanged throughout. The repeated production test takes 227.81 seconds and
confirms nine adoptions, fifteen real workers, three retained assembled mixed
cases, one late-history refusal, 502,422 unchanged-region samples and 508,182
live/export samples. The ten-suite selection also covers history failures,
original-source plans, the application preparation gate, six first-generation
main-window cases, project storage and the extended path test. All three legacy
export hashes remain exact and all observed child processes are reaped.

[Acceptance evidence](../tests/results/M2/2026-10-14-comp-repeated-pairs/index.json)
records the complete selection and scope. Its 13,895,622-byte archive preserves
3,634 original files through 935 unique payloads, with SHA-256
`6ea646d7ce954cb1b559c08e4914c3b8317639cc1643132651119aafc6559824`.
The archive's `files.json` maps original paths to content-addressed payloads.

## Windows correction from original CI

Original CI [38655828613](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38655828613)
passes the Windows cross-build and desktop job. The native worker job passes
16 of 18 tests, including the deep media path regression and version-two worker
suite. Two deeper history fixture setups still fail.

The retained artifact shows one completed write to an unpublished assembly file
with a 263-unit Windows path, followed by a project-store hash check that reports
missing media. Another fixture fails its direct WAV creation call. The correction extends the
existing Windows path conversion to project-store metadata checks, streams,
locking, atomic publication and cleanup, and to the two fixture WAV helpers.
An expanded deep-path test exercises hashing, project save/reopen, backup and
interrupted publication. See [ADR 182](decisions/182-project-store-windows-paths.md).

Linux tests and cross-compiler syntax checks do not establish native runtime
correction. Fresh Windows execution remains required. Stored paths and machine
policy are unchanged.

## Complete original CI audit and next performance work

The original CI run above is terminal and fully retained: all six job APIs and
full logs, all six original ZIP artifacts and all 26 PNGs. Linux partitions are
complete and disjoint over 191 tests: 190 pass, and `comp-clocked-span-worker`
exceeds its unchanged 180-second test deadline. The Windows desktop job passes
26 tests and the cross-build passes. The Windows native job's two failures remain
failures; passing Linux or syntax checks do not replace native evidence.

The original Linux timeout artifact contains six successful worker/verifier
pairs, the required failure cases at 48 kHz, and the preparation result for the
96 kHz case. It has no final aggregate report. This shows progress through the
corpus before the aggregate deadline, but does not establish whether the final
child was stalled. The implementation currently repeats full ancestry review
for both envelopes even when their complete history sets overlap. The next task
is to measure that cost and qualify eliminating redundant review only when a
completed review covers every exact historical record required by the second
source. Both current-source snapshots, distinct histories, freshness, corruption,
cancellation and joined resource accounting must still be checked. Existing
worker, test and job deadlines remain unchanged.

The Windows bundle audit checks all fifteen deployment payloads, both media
library builds and 1,693 corresponding-source files against the Git revision's
Windows checkout filters. Representative Windows and Linux 48 kHz applied-state
screenshots were visually inspected; all 26 images are preserved. This establishes
artifact consistency, not installed or native audio qualification.

[Original CI review](../tests/results/M2/2026-10-14-comp-repeated-pairs/original-ci/index.json)
includes the full logs/APIs, selected reports and all PNGs in a 1,117,192-byte
review archive, SHA-256
`481482d47c4a6dbc564a0ecf81f5869e3857276eb9d249927711bbc4024c698f`.
Large original ZIPs remain in the recorded local retention directory. The draft
PR head is held at the audited revision until the performance correction is ready;
no unchanged rerun or timeout waiver was requested.

## Remaining application work

The low-level paired workflow is available for qualification. The application's
repeated-render preparation entry point remains gated until repeated jobs are
exercised through the real desktop creation/review/Apply workflow. The existing
first-generation desktop workflow remains covered separately. The new cases use
one stereo track; arbitrary layouts, cross-track copies and broader edit/routing
combinations need their own workflow evidence.

Independent processing quality, nonuniform/protected production, earlier
cross-platform sample differences, native installations, full European
localization and the rest of the frozen suite remain open. No installed
application, physical audio route or VM was changed.
