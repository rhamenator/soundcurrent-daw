# Owned protected comp span planning and rendering

2026-10-14 local / 2026-10-15 UTC. Source:
`8e99cbec20a269fa36ce9c678c8ca7926d13e592`.
Full frozen scope remains incomplete. This component is not connected to the
shipping comp worker, publication or desktop Apply path yet.

## Implementation

[Checkpoint 299](299-protected-comp-candidate.md) demonstrated exact anchor
rebasing for expanded protected clips. The new framework-independent
`CompProtectedSpanPlan` owns the raw asset declaration, exact envelope map and
protected geometry. It does not borrow its source envelope or session. The plan
is move-only and returns its resource credit after owned storage retires.

Preparation reserves metadata/map and anchor scratch payloads before copying,
polls cancellation while rebasing and checking knots, and proves equality at the
union of the original and rebuilt piecewise maps' knots. Semantic anchor IDs and
the protection/halo policy survive the transformation. Fractional positions,
changed sample clocks, prior handle artifacts and history regeneration remain
explicitly unsupported by this first adapter.

`renderProtectedCompSpan` uses absolute raw-asset read positions and translates
the rebased origin exactly once. Read buffers are filled with NaNs before the
callback, so a short/incomplete read cannot silently reuse earlier samples.
Input/output bounds, finiteness, exact duration and floating peak are checked.
Cancellation is polled around I/O; failed reads/writes and cancellation propagate
without returning a completion report. Partial output remains the caller's
unpublished responsibility. The existing protected renderer and halo math are
unchanged.

The new processor identity is
`soundcurrent.comp-span-protected-rubberband4-r3-v1`. It is distinct from uniform
positioned comp processing. No request/schema/worker has been taught to accept
that identity yet. Original file admission, child-process ceilings and atomic
publication remain responsibilities of the forthcoming owned-worker adapter.
These APIs run off the audio/GUI threads; they do not establish callback safety.

## Acceptance

Both Debug and Release pass the expanded bank at 44.1, 48 and 96 kHz, with one
and two semantic anchors and three handle extents. Each build records:

- 18 full PCM renders: six nominal controls and twelve expanded regions.
- 264 independent Python `Fraction` map checks and independently calculated extents.
- 34,560 bit-exact protected samples, finite output and retained headroom above unity.
- Twelve refusal/lifetime checks: cancellation before output, render budget,
  incomplete/nonfinite reads, failed write after partial output, cancellation after
  partial output, cancellation during preparation, preparation budget, fractional
  original/expanded positions, moved-from use and a short unqualified edge gap.
- Successful rendering after both source session and envelope have been destroyed.
- Original session equality and returned resource credit after success/failure.

The final Debug qualification takes 22.985 seconds; Release takes 7.058 seconds
including the independent driver. Source/executable hashes and linked libraries
remain stable. All 18 Debug/Release output hashes match. Both new translation
units pass MinGW syntax checks, which do not establish native Windows behavior.
The new test is registered in CTest and added to the native Windows CI build/test
selection; its first native execution is still pending.

[Evidence index](../tests/results/M2/2026-10-14-owned-protected-comp/index.json)
identifies `acceptance.zip`: 9,427,626 bytes, 62 original files represented by 37
unique payloads. SHA-256:
`afa5a5aeba7116c3e3d05ca8944eaa08ac9befefd30ec3c9047714dacf943b84`.
`files.json` maps original paths to digest-named payloads. Full original and output
PCM, reports, logs, build/link information and syntax results are included. Every
payload hash and ZIP CRC was verified. The recorded working-file hashes over
`4184858` match the subsequently committed implementation `8e99cbe`.

## Original production CI confirmed separately

[Original run 38672083523](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38672083523)
at `f868191` completed with all six jobs successful. This validates the preceding
uniform/repeated-comp fixes; it does **not** include the new protected component.
The complete logs report:

| Scope | Result |
| --- | --- |
| Linux shard 0 | 97/97 tests, 669.98 s |
| Linux shard 1 | 97/97 tests, 647.23 s |
| Linux aggregate | Passed; 194 unique tests assigned exactly once |
| Native Windows comp group | 20/20 tests, 163.46 s |
| Native Windows other group | 93/93 tests, 271.57 s |
| Native Windows desktop | 27/27 tests, 165.08 s; whole job passed |
| Windows cross-build | Passed; compilation only |

Native repeated-desktop reports record nine adoptions, three retained mixed cases,
15 successful reaped workers and five additional failure/close cases. Original
outer samples (502,422) and live/export comparisons (508,182) pass, with released
assembly credits. All 14 stage observations finish within the unchanged 30-second
observation limit; the maximum is 3,320 ms. Two retained native Qt screenshots
were inspected: readable transition controls and the applied adjacent-clip timeline
with Ready status. These offscreen workflows are not installed audio tests.

The Windows bundle audit verifies all 15 deployment payloads, both independently
built media dependencies against their reviewed pin, and 1,712 corresponding
source files against the tested Git tree with actual Windows checkout filters.
The bundle is deployment input, not a qualified installer or hardware run.

All six original artifact ZIPs, six full job logs, terminal APIs and 94 PNGs are
retained locally, with original sizes/digests and ZIP CRCs verified before any PR
head advance. The [CI index](../tests/results/M2/2026-10-14-owned-protected-comp/original-ci-index.json)
records every original artifact. `original-ci-review.zip` contains the full logs,
APIs, derived audits/reports and all PNGs: 122 files, 3,673,973 bytes, SHA-256
`4c5d141825ab4940284c46c20ee27b6b78ab97806f60783cfe44a3ead6be330c`.
No rerun, cancellation, timeout change or failure waiver was used.

## Next integration and remaining gates

Add the protected plan to an explicitly versioned owned span request/completion
path. Bind original semantic anchors, normalized raw map, protection/halo policy,
source and output clocks, executable build hash and exact source admission.
Never accept the old uniform identity for this output. Exercise corruption,
interruption, partial publication and rollback before enabling review/Apply.
Then qualify Undo/Redo, save/reopen, repeated regeneration and shared playback/
export in the desktop workflow.

Copied attack cores do not qualify stretched gaps, halo joins, real music or
phase-sensitive recording groups. [ADR 104](decisions/104-local-warp-and-group-quality.md)
continues to define the independent quality gates. Fractional/post-SRC timing,
history regeneration, arbitrary channels, native Windows runtime, current
installed delivery, all-Europe and the entire M2–M11/F/Q/C/N/X004/X005 scope remain
required. No installed application, audio route, VM or equalizer was changed.
