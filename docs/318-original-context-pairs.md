# Original-context rendering through paired edits and the desktop

Date: 2026-10-15. Production implementation: `26367a8a3a6cee56483658a010e612f25102ae2e`.
Corrected test driver: `a5fc7d6d3446d8fa70175738aa92af3bd6868414`.

## Delivered workflow

The application now selects the original-context renderer when a paired edit has
regeneration history and a uniform side needs processing. Uniform children use
the original-context worker; protected children retain their original geometry
through the clocked producer. An existing assembled side can remain a retained
input without starting a worker for it.

The selection has its own persisted format,
`sc-comp-pair-selection-original-context-v1`. Reopening rederives the exact child
requests and checks their saved bytes and digests. Older selections retain their
recorded producers. New selections choose the producer before execution; no failed
legacy render is silently retried using a different algorithm.

The desktop path reaches this through the existing speed, Save and render,
review and Apply controls. Rendering does not adopt audio. Apply changes both
clips in one undoable edit, and saving remains explicit. The processing changes
are isolated from the audio callback. See [ADR 202](decisions/202-original-context-comp-pairs.md).

## Acceptance evidence

[Portable evidence](../tests/results/M2/2026-10-15-original-context-pairs/index.json)
contains source/executable hashes, commands and logs, actual owned project/media
history, requests, rendered PCM, exports and screenshots.

- The new paired suite passes in Release (35.32 seconds) and Debug (80.40 seconds).
  Each performs three adoptions, five worker renders and one retained mixed case.
  Recomputed candidate audio agrees across 320,340 samples; unchanged regions and
  130,576 live/export samples are checked. Undo/Redo, save/reopen, stale Apply,
  early history corruption and later verification corruption are exercised.
- Each of the three saved pair selections rejects a downgraded format and a
  changed child digest, even with the outer hash recomputed. The exact original
  selection reopens successfully.
- The desktop short-edit suite passes in Release (45.09 seconds) and Debug
  (110.45 seconds), each with three adoptions and five reaped workers. It checks
  320,340 candidate samples, 122,682 unchanged-region samples and 130,576
  live/export samples. The shortened fractional crop emits 4,054 frames from
  34,828 original-context frames and preserves baked pitch factor 1.5. The old
  crop-local processing plan is below the 4,096-frame backend window.
- All six paired/desktop export PCM files are byte-identical across configurations.
  Every new worker file's whole-file hash, sample hash, frame count, finite peak
  and explicit memory-credit bound is audited. Floating-point headroom remains.
- Nine new desktop PNGs and three render observations are retained per build.
  Maximum observed stages are 8,414 ms Release and 19,538 ms Debug, both within
  the unchanged 30-second bound. One Release short-edit review screenshot was
  visually checked; its status and Apply controls are readable.
- Additional Release checks pass for the legacy mixed paired producer, initial
  desktop transitions and ordinary repeated desktop edits. The latter performs
  nine adoptions and fifteen worker renders at three source rates. The three
  legacy mixed-pair export hashes match their preceding qualified results.
- Three changed C++ units pass MinGW syntax checks. Native Windows execution of
  this source and installed preview qualification remain pending.

## Initial failure and source attribution

The first Release batch passed four tests and failed the new pair test: the test
driver passed `originalContext` on start but omitted the fixture's required
`protected`/`clocked` flags. The fixture refused the start acknowledgement.
The correction carries those flags on every command in the new optional mode.
Production code and all compiled Release binaries are byte-identical before and
after this one-line driver change. The failed run remains failed and is archived.
Only that failed Release test was repeated; the already-passing desktop and legacy
results remain bound to their original receipt and identical binaries. Both Debug
tests ran with the corrected driver.

The first independent audit used the prior standalone worker fixture's 4,041-frame
crop. The paired workflow starts at a different source position and produces
4,054 frames. The audit now requires that exact count and its 34,828-frame context;
no renderer or test admission check changed.

## Delivery status and next task

The preceding [CI run 38729254730](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38729254730)
at `de56b1d4a79d3361a50798172afd49cbb9ff9a25` passed all twelve jobs. All
220 Linux tests ran in five disjoint shards; native Windows passed 42 comp and
93 other tests, plus the existing desktop, import/localization and cross-build
jobs. All twelve complete logs, eleven original artifact ZIPs and 270 PNGs were
retained and audited before advancing the draft. The
[portable CI review](../tests/results/M2/2026-10-15-original-context-pairs/original-ci-index.json)
includes all review metadata, logs and PNGs, plus the complete new native
original-context core/worker corpora; all original ZIPs remain locally retained.

On that preceding source, all seven core PCM files match Linux byte-for-byte.
The native owned worker passes three candidates, two seed workers, 33 refusal
cases and 210,770 candidate comparisons. Two candidate outputs match Linux PCM
exactly; the remaining history case has maximum absolute difference
2.9802322387695312e-8. These results qualify the core and worker; they do not
qualify this newly integrated paired/desktop source.

CI adds the new pair suite to the native comp bank and complete Linux inventory,
with its evidence retained. Expected inventory: 221 Linux tests and 43 tests in
the first native comp bank, followed by the existing 93-test bank. Scheduling
estimates do not change any deadline.

Next qualify this paired/desktop source natively and prepare updated Linux and
Windows preview packages. No installed app or physical audio route was changed
by this work.

Newly rendered PCM can differ from older crop-local results; saved audio is not
rerendered on open. Genuinely short whole original domains, long-context processing
time, broader material/layout quality, localization and full DAW parity remain
open. Passing these synthetic workflows does not establish processing-quality
parity or justify a release claim.
