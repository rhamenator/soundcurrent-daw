# Comp spans with explicit output sample clocks

Checkpoint 291, 2026-10-14. Full DAW goal remains active.

The direct uniform-span renderer now distinguishes the original raw sample rate
from the current artifact's output rate. It uses original nominal pitch settings,
accounts for varispeed already baked into historical assemblies, and leaves the
current clip's playback rate separate. Exact output duration and floating-point
headroom are retained. See [ADR 176](decisions/176-comp-span-output-clock.md).

## Evidence

[Qualification](../tests/results/M2/2026-10-14-comp-clock-spans/index.json) binds
source `e50e85f630b4d06e4c56c5f343b7be8c34730211`. Four suites pass in 76.93 seconds:

- Fifteen direct clock-aware renders use verified raw tones at 44.1, 48 and 96 kHz
  and produce 48 kHz PCM. Cases include physical and project-domain histories,
  fractional cropping, additional metadata generations, and an independent
  current playback-speed edit. Baked factors range from 1 through 3.375.
- Dominant and requested-frequency peaks remain within the 1% experiment guard;
  maximum observed relative error is 0.56897%. Float peaks range from 3.41 to 4.22.
  These observations do not establish reference processing quality.
- Thirty-six legacy uniform renders, six old span-request/recipe identities,
  twelve layered planning cases and mixed-pair protocol refusals pass.
- All legacy export file hashes match checkpoint 290 exactly.
- Original-history verification still passes six principal source reviews,
  405,432 PCM comparisons, 393 assertions and 132 refusal cases. Project bytes
  remain unchanged, joined resources are released, and observed processes retire.

The archive contains 1,306 entries and 42,033,850 bytes. It includes PCM, source
and binary hashes, original workflow evidence and logs. The first qualification's
fixture failure is retained separately: it incorrectly expected the 48 kHz
physical-domain assembly to have baked its playback speed. The corrected test
derives that expectation from the stored assembly domain. No deadline or pitch
guard was relaxed.

## Original CI retained

[Run 38646658520](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38646658520)
at `0ead497` completed. Linux passed all 187 tests; the Windows cross-build passed.
Both native Windows builds stopped at the JSON/string-view comparison ambiguity
already corrected in checkpoint 290. There was no native deployment bundle.

The [audit](../tests/results/M2/2026-10-14-comp-clock-spans/original-ci/audit.json)
records all four full job logs and all four original artifact archives, checked
against GitHub's byte counts and SHA-256 digests and against ZIP CRCs. All 12 PNGs
are retained; a Linux creation-dialog screenshot was inspected. Linux's twelve
layered recipes, six legacy recipes and eight creation cases/180 assertions pass.
Fresh native Windows confirmation remains required.

## Integration still required

These are direct DSP renders, not completed regenerated jobs or UI operations.
The owned span/pair protocol still rejects regenerated recipes, including mixed
pairs with an unchanged assembled side. Its version-one bytes remain unchanged.

Next: version the request/intent clock fields; write and verify output WAVs at
the declared rate; review original historical files around production; and carry
the full dependency guard through queued canonical adoption. Then qualify actual
repeated jobs, mixed pairs, retained assembled takes, Undo/Redo, save/reopen,
export and unchanged audio outside the edit before enabling the UI workflow.

Nonuniform/protected production, independent quality gates, the earlier tiny
cross-platform PCM discrepancies, native installation and full European
localization remain open alongside the remaining suite milestones. The installed
application and audio routes were not changed.
