# Source dependencies retained through queued comp Apply

Checkpoint 293, 2026-10-14. The full DAW goal remains active.

An assembled comp edit now carries an immutable dependency snapshot through the
Apply queue. The canonical session owner checks that its project clock, assets,
saved histories, selection references, retained takes and channel layouts still
match before accepting the edit. The snapshot follows both paired outputs and
their ancestors even when the selection initially contains only one output.
Unrelated project names, track names and track EQ settings may change normally.
See [ADR 179](decisions/179-queued-comp-dependencies.md).

The snapshot uses the same admitted resource ledger as the verified adoption
token. It retains its credits until the last queued reference releases the data.
It contains metadata only; separate owned-file and PCM verification remains
required. The real-time callback performs none of this work.

## Qualification scope

The source-bound qualification covers six suites: CI partition coverage, core
atomic adoption, layered source dependencies, original saved-history review,
version-two child workers, and the desktop Apply workflow. The desktop cases
exercise actual first-generation Review/Apply/Undo/Redo/save/reopen/export plus
changed-file, reopened-project and close/cancel refusals. Layered metadata cases
test dependency closure through extra generations; they are not repeated rendered
and adopted generations. Stale ancestry is tested in the framework-independent
guard, not through a new GUI mutation test.

[Qualification evidence](../tests/results/M2/2026-10-14-comp-adoption-dependencies/index.json)
at `bbd5aa5d8db3f339c744a8d5f0366f9959f3898e` passes all six suites in 172.64
seconds. It records six historical guards with 48 stale-dependency refusals,
twelve layered guards with 42 ancestor-change refusals, 531 history assertions
and 186 total history refusals. Nine version-two workers complete and four
required failures are retained. Full comparisons cover 405,432 historical and
350,902 new-worker PCM samples. Three normal desktop exports match prior WAV
hashes exactly, and every observed child retires.

The archive preserves all 2,745 original payload paths through a SHA-256 file
manifest and 647 unique blobs, totaling 9,148,218 bytes. Deduplication removes
identical stored copies without discarding their bytes or original identities.
The 48 kHz Review and applied-timeline images were visually inspected. The new
core translation unit also passes Windows cross-compiler syntax checking; native
execution remains due.

The initial qualification passed five suites but the new history fixture crashed
when its unrelated-EQ-change check indexed an empty band list. The fixture now
adds a band explicitly. Its wrapper retains reviewer process exit codes before
asserting success. Original failed logs and inputs remain retained.

## Original CI and Windows bundle review

The previous original run
[38650069106](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38650069106)
is terminal and retained before advancing the draft PR. Its Windows desktop
suite passed 26 tests and the cross-build passed. The native worker suite passed
14 of 16 tests; the two original history fixtures failed during setup. The
Windows path correction from checkpoint 292 still needs a fresh native run.

Linux reached the whole-job deadline after 92 of 188 tests passed. Its remaining
tests were unexecuted. The next CI run uses two complete, disjoint CTest
partitions with the existing deadlines and no omitted tests. The exact required
aggregate check name is retained; branch protection is unchanged. Local CTest
selection agrees with both planned partitions, containing 93 and 94 tests in
this local configuration. See [ADR 180](decisions/180-complete-linux-ci-partition.md).

All five original archives are retained locally with size, digest and CRC checks.
The [public review bundle](../tests/results/M2/2026-10-14-comp-adoption-dependencies/original-ci/index.json) preserves terminal APIs, all four complete logs, all
26 original PNGs, and selected reports. Three representative Windows images were
visually inspected: the narrow crossfade dialog, transition creation, and the
applied timeline. Retaining every image does not mean each was visually reviewed.

The fifteen-file Windows deployment bundle passed independent byte/source
auditing, including 1,674 corresponding-source files compared using Git's actual
Windows checkout filters and both media builds against the reviewed dependency
pin. This is artifact verification, not native installation or audio evidence.

## Next production step

Split internal file-only assembly verification from public history-aware
verification so the iterative history reviewer cannot recurse. Use original
nominal clip settings for regenerated sources, then integrate history review
around paired rendering and adoption. Keep the production pair gate until actual
repeated pairs, mixed pairs, retained assembled takes, persistence, Undo/Redo and
unchanged PCM outside the edit pass.

Independent processing quality, earlier cross-platform sample differences,
native installations, full European localization and the full frozen suite
remain open. No installed application, physical audio route or VM changed.
