# Original nominal render review for comp assembly

Checkpoint 279 requires comp assembly to review the original nominal render of
each processed fragment. The production path no longer accepts an arbitrary
current WAV and a plausible stretch anchor as sufficient nominal provenance.
It still produces candidate PCM, with final publication/adoption and processing
quality remaining open.

## Original receipt binding

The canonical nominal asset identifies the original operation UUID and exact
`media/derived/<operation>/audio.wav` path. For an already expanded handle window,
the separately retained nominal asset is used. The original job must have a
retired, readable ownership lease. Assembly derives the original plan from the
current raw asset and preserved nominal stretch anchor, then invokes the existing
owned-stretch verifier under policies supplied by the trusted caller.

The resulting asset and anchor must exactly match the canonical nominal asset and
anchor. This includes original raw source/hash/extent, processing geometry,
settings/render key, original completion shape, policy, RF64 geometry, full file
and decoded sample hashes and float peak. Review occurs before any output and
again after assembly, for both processed fragments. A missing or changed original
receipt, busy original lease or policy mismatch refuses the operation. No original
files or project state are modified by review.

Original policies default to the existing application stretch policy and can be
provided separately for each side. The caller must retain or independently know
them; reading a grant from the receipt being verified would defeat that check.
Integrating historical nondefault policies into the final project/UI recovery
workflow remains required. Imported derivatives without an owned original receipt
need explicit regeneration from retained raw material or another qualified
provenance path. They are not silently relabeled as owned renders.

The older stretch protocol binds processor/geometry/content but does not include
an executable digest. This review therefore does not attest the original binary.
The acceptance harness independently records the binaries it actually launches.
Adding executable identity to the original stretch protocol, where required for
future final-job trust, must be versioned without pretending old markers contain
that information. Neither original receipts nor correct blend arithmetic establish
sound quality or safe automatic model adoption.

## Real producer acceptance

[Source-bound Linux evidence](../tests/results/M2/2026-10-14-comp-nominal-provenance/index.json)
binds implementation `1f49332197d134fb53e3b69e5465cdc5586ddd50`, all relevant
source/library inputs and five executables. CTest process 3896607 passes in 9.50
seconds, with inputs unchanged before and after. The earlier development run
passes in 9.49 seconds and remains retained locally.

The fixture starts with controlled raw audio and analytic placeholder derivatives.
It then prepares real nominal requests through `prepareClipStretch`, launches six
actual `sc-stretch-render-worker` processes, verifies readiness before start,
observes retirement, independently verifies completions, and submits the actual
`ApplyClipStretch` edits through `EditHistory`. Exact Undo/Redo and save/reopen
pass. Original raw assets and retained takes remain unchanged. Comp preparation
uses only the resulting accepted nominal assets and anchors; placeholder audio is
not used as its baseline.

Six additional actual span workers then complete, and assembly produces two
physical and four post-SRC fragments. Stereo source rates are 44.1, 48 and 96 kHz,
with a 48 kHz project, 3/2 stretch, +700.007-cent pitch, fractional raw origins and
independent playback timing. All twelve child processes are reaped.

All 202,620 unblended samples are bit-identical to the oracle; all 96 independently
computed quintic blend samples match their rounded floats. Separate scope review
compares 98,272 physical and 202,092 project samples exactly. Minimum output peak
is 3.6371512413, preserving float headroom. The positioned-FIR oracle uses the
existing kernel at different block sizes; independent FIR/sonic qualification is
not inferred from these results.

The corpus retains the previous incomplete-pair/stale-state/reference/quota/
cancellation/sink/late-child-marker refusals. It adds an actual exclusive original
OS job lease, a wrong original memory policy, and original completion corruption
both before output and during streaming. No failed sink is published, and assembly
leaves canonical/saved state unchanged. Nominal adoption is separately exercised
and is explicitly distinct from final paired comp adoption, which is not present.

The archive contains actual raw/nominal/span/assembled WAVs, original nominal
requests/readiness/completions, accepted render keys, project generations, paired
job evidence and all process/source receipts: 5,556,497 bytes, 221 entries,
SHA-256 `bb3136a9b07e939f0c85872bc67966dcb8f6b34ad91c346ec0a5f8e142bcf8f5`.

## Original Windows predecessor retained

[Original CI 38616747650](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38616747650)
at `1545c01` passes all four jobs: Linux 180 tests, Windows core cross-build,
native Windows 93 plus 11 explicit gates, and desktop 23. Its original terminal
APIs, full logs and all five ZIPs were retained and checked against server sizes,
SHA-256 digests and ZIP CRCs before advancing the PR head.

[The retained review](../tests/results/M2/2026-10-14-comp-nominal-provenance/pr194-supervisor-ci/index.json)
includes the actual native supervisor corpus: ten cases, nine child processes,
three successful spans, all children reaped, including the unchanged deadline
watchdog. Native trusted project-reference schema tests also execute. Independent
bundle audit verifies 14 deployment payloads, 1,607 corresponding source files
using actual Windows checkout filters, collection hashes and both media DLL pins.
The public review is 479,184 bytes/179 entries; complete original waveform ZIPs
remain locally retained. This predecessor qualifies schema/supervision, not the
new assembly/original-nominal path. New Windows execution is pending; no main-window,
native hardware audio or installer qualification is inferred.

## Next implementation

Implement an owned final assembly job with immutable publication, intent/completion
and recovery, binding the current trusted selection, original nominal policies
and actual assembler identity. Independently verify the resulting complete PCM,
curve/alignment and permitted scope. Add shared live/offline post-SRC representation,
versioned persistence and paired atomic adoption/Undo before exposing Apply in
the main window. Preserve existing raw/take/nominal anchors and close/cancel guards.
Nonuniform/protected span production, broad channel/rate/container/group/platform
coverage and existing spectral/mixed-join quality gaps remain required.

No UI, installed preview, audio route, VM, equalizer repository or dependency
changed. Localization remains 1,038 keys, 34 partial catalogs, 3,168 drafts, zero
reviewed and zero UI-qualified languages. Full frozen DAW parity remains incomplete.
