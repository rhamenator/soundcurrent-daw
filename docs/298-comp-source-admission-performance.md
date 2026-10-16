# Repeated comp source admission performance

**Evidence correction, 2026-10-15:** the repeated-desktop cleanup assertion
reported below observed an unused options ledger, not the actual project ledger.
It did not establish actual project-memory release. Original reports are retained;
[checkpoint 304](304-protected-desktop-transitions.md) corrects the fixture and
qualifies cleanup locally. Native confirmation of that correction is pending.

Checkpoint 298, 2026-10-14. Full DAW goal remains active. The installed preview remains unchanged; source qualification is complete locally.

## Measured failure and correction

The preceding Debug probe failed after its first successful repeated adoption.
New test-only stage observations preserve the exact stage at failure without
changing the 30-second observation limit. The second edit spent about 17.2 seconds
preparing/rendering spans, reached the end of publication at 26.5 seconds, and was
still in final assembly verification at 30 seconds. Its original failed project,
observations and logs remain retained.

A bounded partial profile of historical verification recorded 24,101,136,007
instruction references before its 90-second profiler limit. Source admission made
494 WAVE-validation calls, accounting for 63.67% of recorded instructions. All WAVE
validation accounted for 68.92%. This incomplete instrumented profile identifies
work to inspect; it is not a complete workflow or a native timing result.

The same exact asset can occur as raw/current/retained/nominal media on both sides
and in their histories. [ADR 187](decisions/187-per-admission-comp-source-checks.md)
shares completed checks within each admission call, using full Asset equality and
a bounded, resource-charged list. A declaration is recorded only after its WAV and
all companions pass. Each later admission starts fresh. Pre/post verification,
source history and output PCM checks remain in place, as do all deadlines.

At source `0e625bdbfa7d56ca82d4b5281576d8e76a9a9c1c`, the same 44.1 kHz Debug
workflow passes all three adoptions in 90.80 seconds, including Review/Apply,
Undo/Redo/save/reopen/playback/export. Render-and-review stages take 17.677,
20.805 and 18.553 seconds. The first diagnostic stage took 23.841 seconds; its
second stage failed at 30.003 seconds. This is one before/after workflow comparison,
not a host-independent speed guarantee or complete Debug qualification.

Additional source-admission tests alter a raw file after a successful admission,
require the next call to refuse it, restore the original bytes and admit again.
Cancellation and insufficient memory must release temporary credit. Three sample
rates, repeated and mixed retained sources, worker failures and history guards
are covered by the focused qualification below.

## Source-bound qualification

At `0e625bdbfa7d56ca82d4b5281576d8e76a9a9c1c`, all four selected Debug suites pass
in **749.92 seconds** and all eleven selected Release suites pass in **391.44
seconds**. Each build's selected targets and dependencies were rebuilt before a
Ninja no-work check. Sources and binaries remained unchanged during each run.

| Workflow | Debug seconds | Release seconds |
| --- | ---: | ---: |
| Paired history review | 102.96 | 36.62 |
| Clock-aware span workers | 126.66 | 40.86 |
| Repeated paired production | 216.86 | 79.08 |
| Repeated desktop workflow | 303.42 | 118.49 |
| Verified pair assembly | — | 7.55 |
| Owned assembly publication | — | 19.04 |
| Regeneration source | — | 22.06 |
| Regeneration history | — | 24.51 |
| Owned span worker/fault workflows | — | 4.74 |
| Desktop assembly Apply | — | 19.37 |
| Desktop first-generation creation | — | 19.11 |

The complete Debug desktop suite performs nine actual adoptions and five further
failure/cancellation cases at three source rates. All fourteen recorded render
observations finish without a timeout; the longest successful stage is 20.666
seconds. All controllers release their assembly credits. The slowest successful
Debug core repeat worker takes 6.298 seconds against ten seconds; the slowest
clock-aware worker takes 5.014 seconds. These are observed margins on this host.

Each configuration verifies nine successful clock-aware workers and four required
refusals, with 350,902 complete PCM samples compared. All nine fresh-admission
checks detect changed bytes after an earlier successful call, accept restored
bytes, and release temporary credit on cancellation and budget failure. All
observed workers retire without observation timeout or pending retirement.

All nine core and all nine desktop export hashes match their respective preceding
qualifications, in both configurations. The three legacy Apply hashes also match.
Desktop workflows compare 502,422 unchanged outer samples and 508,182 live/export
samples per configuration. The three mixed desktop/core cases use different
incoming source/fade fixtures, as documented in checkpoint 296; their cross-family
hashes are deliberately not presented as equivalent. This is integrity and
renderer-consistency evidence, not independent acoustic-quality admission.

The Release repeated-desktop suite previously took 233.90 seconds and now takes
118.49 seconds with matching exports. This is an observed comparison of these
owned workflows, not a general throughput guarantee. Two modified translation
units pass MinGW syntax checks; native Windows execution still needs fresh CI.

## Retained evidence and remaining gates

The [evidence index](../tests/results/M2/2026-10-14-comp-source-admission/index.json)
and its `acceptance.zip` retain both complete qualifications, original corpora,
source/binary hashes, failed stage diagnostic, bounded partial profile, successful
focused Debug probe, build logs and export/resource audits. Every original payload
is indexed by name in `files.json` and verified by digest and ZIP CRC. The original
checkpoint 297 failure and CI artifacts remain untouched. The actual Debug window
after a second 96 kHz adoption was visually inspected.

Native CI confirmation is pending.
Independent sound-quality admission, current installed delivery, nonuniform and
protected comp production, all-European language review and the complete frozen
professional scope remain incomplete.
