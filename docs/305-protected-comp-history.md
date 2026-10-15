# Repeated protected comp rendering

2026-10-15. Source `994e83cb60eaec24e2602c3da6e37ad2476e7b98`. The full DAW goal remains incomplete.

## Original semantic history

Protected span preparation now follows the original nominal clip through an
assembled clip's retained history. It takes the original raw asset and semantic
attack-marker identities, locates those raw positions on the current exact
piecewise map, and reconstructs the protected windows in the expanded output.
It compares the reconstructed map with the original map at the union of their
knots. Current assembled PCM is never treated as the original raw recording.

The plan owns its raw declaration, map and geometry. Preparation reserves memory
before allocating anchors. The render path stays on the existing bounded,
off-audio worker and preserves floating headroom. Source and historical receipt
verification still run before output and before completion/publication/adoption.

This adapter admits complete original protected windows at integer positions,
unchanged sample clocks and unchanged baked pitch. Fractional positions, changed
clocks/pitch, cropped protected windows and direct legacy handle-window inputs require
further adapters. They remain required product work; no rounding or uniform
fallback is introduced.

## Versioned history request

`sc-comp-span-protected-v2` and
`soundcurrent.comp-span-protected-rubberband4-r3-v2` distinguish regeneration from
original protected v1. The existing paired selection format retains the trusted
child request digest; its rederivation now selects v2 when either side requires
history review. A protected original side paired with retained historical media
also binds that review requirement.

The request, readiness, intent and completion bind original semantic geometry,
raw media identity, input/output clocks, baked pitch, the original clip identity
and whether this side was regenerated. The recipe continues to bind the complete
paired dependency state. Decoding reconstructs and compares those values; changing
a binding or downgrading the request cannot discard the history requirement.
Original protected v1 and uniform v1/v2 formats remain unchanged.

## Executed workflows

The new bank starts with real owned nominal and protected-span workers and real
first assembly/publication/adoption. Three sample rates (44.1/48/96 kHz) each run
three further generations: two extended protected pairs and one transition from
a retained assembled take with existing handles to an original protected clip.
The existing side creates no span worker. The bank drives canonical engine/model
APIs, not a simulated adopted state or a desktop shortcut.

Every generation publishes, verifies and adopts owned output; checks exact
Undo/Redo and save/reopen; compares live playback in 193-frame callbacks with
127-frame WAV export; retains floating headroom; compares protected cores
bit-for-bit to the original raw samples; and returns all joined memory credits.
The historical completion is altered before publication and verification to
require refusals. A late mutation before completion also refuses publication.
A stale queued Apply cannot mutate the canonical project.

Two extra real workers exercise cancellation while waiting for start and a changed
historical completion after readiness. Both retire without a completion marker.
Fresh-process inspection sees Canceled or Interrupted with no accepted pair.
Request mutations cover clock/pitch, original identity, geometry and a version
downgrade. They are rehashed by the fixture to ensure rejection does not depend
solely on a mismatched outer digest.

## Audible edit scope

The first development run exposed an incorrect playback assertion: it compared
outside only the newly selected fade while replacing an earlier, wider fade.
Changing a crossfade changes both its old envelope and the new envelope. The
corrected assertion derives the union of the affected original outgoing fade-out,
original incoming fade-in and new transition, then checks unchanged playback
outside that union with the existing 128-frame processing-edge guard. It reports
both requested and audible edit bounds. It does not enlarge the permitted source
PCM blend regions; full source assembly/recomputation checks remain separate.
The failed original run and subsequent correction are retained as evidence.

## Qualification

Final source `994e83cb60eaec24e2602c3da6e37ad2476e7b98` passes one Debug
suite in 278.298 seconds and five Release suites in 236.122 seconds. Source and
binary hashes remain stable throughout; selected Ninja targets have no pending
work. Each new bank performs nine canonical adoptions, three retained mixed cases,
15 successful span workers and two required worker failures. All children retire
and all joined resource credits return. It checks 32,000 original protected raw
samples, 1,640,832 unchanged audible samples outside the recorded affected range,
and 1,810,560 live/export samples per configuration.

All 15 new span and nine export hashes match between Debug and Release. The 14
initial assembly/export hashes match the prior bank. Release also preserves six
original protected-worker, nine legacy repeated-export and nine uniform clocked
span hashes. Three changed C++ translation units pass MinGW syntax checking with
no diagnostics; this does not establish native Windows execution.

The qualifier's first Release preflight rejected the core test's positional
evidence argument before launching any CTest. The script now recognizes that
specific argument form, and the original preflight failure remains retained.
The initial development playback-scope assertion failure, corrected probe,
final tests, complete project/media/process artifacts and audit tools are in the
[evidence index](../tests/results/M2/2026-10-15-protected-comp-history/index.json).
Its verified archive contains 7,297 original files in 2,185 unique payloads,
52,766,434 bytes, SHA-256
`80aebcf0cd0a7bc74e882274ebed12e70105cd3501a9cb18b5f35aa03a28276c`.

## Preceding original CI audit

[Run 38681796679](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38681796679)
at `9a3cd5b8dba1b7287ae42138344b6e372e317c6f` predates these changes.
Its Windows desktop job passed, but the native core job failed
`pipewire-owned-recording`: check 2, the short one-frame finite completion.
All six jobs have now finished and the run is **failed**. Both Linux CTest
partitions passed (100/99, 199 unique tests assigned exactly once). Shard 1 reached
the unchanged 30-minute job limit during the later retained short-span observations
step; eleven subsequent archive-verification steps were skipped. The remaining
Linux checks therefore did not qualify, despite all CTest cases passing. Native
Windows passed all 24 comp tests and 92 of 93 other core tests, plus all 28 desktop
tests. The cross-build passed. No unchanged rerun, manual cancellation or relaxed
deadline was used. Scheduling and the native startup failure require correction.

All six original artifact ZIPs, terminal APIs, full job logs and 146 PNGs are
retained. Original sizes, digests, ZIP CRCs and disjoint test inventories were
verified. The [original CI review index](../tests/results/M2/2026-10-15-protected-comp-history/original-ci-index.json)
keeps the native protected/assembly/desktop corpora, reports, full logs and
screenshots in a verified portable archive; full original ZIPs remain local.

Scoped native confirmation is now available for the preceding assembly and
desktop work: five protected assembly pairs, 110 processes, 15 abrupt publication
exits; twelve protected desktop cases with five adoptions and nine retired span
children. The corrected actual-project ledger assertions pass. Legacy repeated
UI also passes nine adoptions, three retained mixed pairs and 15 reaped children,
with fourteen observations below 30 seconds (maximum 6,350 ms). The prior vacuous
options-ledger result remains preserved; this new source tests the real ledger.
Two protected native Qt images were inspected for readable applied/review controls.
Fifteen deployment payloads and 1,734 corresponding-source files match the native
checkout, and both media builds match the pinned dependency. These scoped outcomes
do not qualify the failed full CI run, installed audio, or this newer history code.

The [retained failure subset](../tests/results/M2/2026-10-15-protected-comp-history/preceding-native-failure.json)
contains the complete original native job log and 13 original failed-project files,
checked against their original hashes. The manifest lists one additional empty
`.save.lock` that the original artifact omitted; it is explicitly recorded as
missing and has not been reconstructed. It records 96,480 finalized raw
frames, zero delivered monitor frames, a published one-frame startup receipt,
and joined capture/render owners. The stored render failure is `Startup` (19),
with no disk/storage or processing fault. The full native artifact ZIP is retained
locally with its original size/digest and CRC verified.

Code inspection identifies two possible snapshot races: the PipeWire consumer
reads accepted frames before acquiring readiness; both native consumers compare
a newly published readiness timestamp with a clock sample taken before that
publication. Either can reject an otherwise valid concurrent handoff. The original
failure does not record which subcondition failed. Deterministic interleaving
experiments must establish the cause before choosing a correction. This is not
evidence for relaxing the readiness checks or deadlines.



No production UI strings or headers changed; no localization extraction is needed
for this increment. The repeated protected desktop interaction bank, native
Windows runtime for these new changes, installed preview delivery and independent
sound-quality qualification remain pending. This work does not complete M2,
M3–M11, F/Q/C/N, X004/X005 or all-Europe localization. No installed app, physical
audio route, VM or equalizer repository was changed.
