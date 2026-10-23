# Protected comp handles: isolated feasibility

2026-10-14 local / 2026-10-15 UTC. Source: `f68af1266a414fd385492dbe80c87345d8e605b8`.
The full frozen goal remains incomplete. This is an opt-in experiment, with no
application, worker-protocol, publication or installation change.

## What was tested

The shipping uniform comp producer still refuses nonuniform maps. The existing
protected-warp renderer already preserves designated attack cores, but its input
region and anchor positions must be adapted when comp handles expand a clip.

The new [candidate](../experiments/comp-protected/README.md) reconstructs a protected
plan from the original semantic anchors. It shifts the source origin and output
prefix without rounding, preserves anchor IDs and the protection/halo policy,
and verifies the union of the original and reconstructed map knots before reading
PCM. Its read callback adds the raw origin exactly once. It uses the existing
renderer and pinned dependencies without changing their processing code.

The first supported experimental subset is an original protected clip with
integer acoustic positions, unchanged sample clock, zero pitch, preserved
formants, and no extra context. Previous handle artifacts and regeneration
history are refused. Fractional source origins and expanded endpoints are also
refused. These restrictions describe this experiment, not a reduction in scope.

## Evidence

A Release build produced nine successful renders: three nominal controls and six
expanded regions, at 44.1, 48 and 96 kHz. Each fixture has a marked attack over a
sustained bed, stereo delay/polarity relationships and samples above unity.

The independent Python oracle checked:

- 105 exact rational map queries, including both maps' knots and interval midpoints.
- Independently calculated source and output extents, exact duration and finite PCM.
- 11,520 protected-core samples that match the original raw bytes exactly.
- Floating-point headroom above unity in every output.
- Cancellation and memory-budget refusal before output, returned resource credit,
  and distinct refusals for fractional original and expanded origins.
- Stable source and executable hashes through the run.

The C++ fixture separately verifies that the session/source declarations remain
unchanged. The final refusal checks require the expected diagnostic, so another
failure cannot masquerade as a fractional-position refusal. MinGW syntax checking
passes; this does not establish native Windows execution. The original linked
static-library hashes and link command are retained.

[Evidence index](../tests/results/M2/2026-10-14-protected-comp-candidate/index.json)
identifies the 23-file archive, including complete source/output PCM, reports,
stdout/stderr, build/link information and syntax results. Archive size is
5,850,718 bytes; SHA-256:
`2388c41b5f86ac6a720759733e41eb4614cf5f07b2043ce6361d2efde2bbe91e`.
Every archived payload and ZIP CRC was verified. The evidence records the tested
working-file hashes over base `f868191`; those exact source files were then
committed as `f68af12` without further implementation changes.

## Remaining work and next implementation

A copied attack core does not qualify sound quality in the surrounding stretched
gaps, halo joins, real music, or phase-sensitive microphone groups. The existing
[quality contracts](decisions/104-local-warp-and-group-quality.md) still apply.
This experiment also does not qualify multiple anchors, fractional grids, changed
sample clocks, repeated history, native Windows runtime, or a desktop workflow.

Next implement an owned protected-comp plan and renderer for the demonstrated
integer-clock subset, with a distinct processing identity, exact original map
and anchor provenance, cancellation/deadline/memory limits, and no silent routing
through the uniform processor. Extend independent tests to multiple anchors,
edge/short gaps, corrupted input and failure during processing/publication before
integrating review/Apply, Undo/Redo, save/reopen and repeated regeneration.
Fractional timing and post-SRC/history paths remain required subsequent work.

Original CI run `38672083523` at `f868191` is a separate qualification of the
previous production changes. Its terminal original APIs, full logs and all
artifacts must be retained and audited before the PR head advances. This
experiment does not replace those results or authorize an unchanged rerun.

No installed application, audio route, VM or equalizer repository was changed.
The CMake experiment option defaults OFF; default test inventory is unchanged.
All broader M2–M11, F/Q/C/N, import, equipment, language and delivery gates remain
open.
