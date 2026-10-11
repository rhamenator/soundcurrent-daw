# Versioned native startup state and explicit legacy migration

2026-10-11. M2 progress. Full frozen functional, processing-quality, content and
native-project scope remains open, including M2–M11, imports, equipment profiles,
Windows parity, installation and European localization.

## Preceding protected composition

PR141 passed all four required jobs on38165380045: native MSVC71 suites160.35s,
new startup composition0.09s, and three original20-take/five-second completions.
Exact head6c2a90344e52b61d955b5dc0efc84a0a5ebf9f8e, base
7d800f210258d77c82b3f43fa904c6e1a29d9a6c, tested merge
e5364ad7c2b4c20a1e6cc7277fd5c570ab383924, tree
3b8cb50a2981b3c37392195493500070b3d91a56 and66 reviewed blobs were bound
to all four full original job logs. Enforced strict checks, app identities, latest
results, resolved threads, clean candidate and current MAIN were verified before
an ordinary matching-head squash. MAINc5e15a119f697fb3cf284a8206827273d74398e3
matches the tested whole tree, base parent and66 blobs; API and remote refs match.
The [protected packet](../tests/results/M2/2026-10-11-native-ready-protected-main/summary.json)
retains original bytes, admission and postmerge receipts. Earlier Windows failure
roots remain unresolved; later success is bounded non-reproduction, not a causal
correction or physical audio qualification.

## Auxiliary schema1.1

New native-run writers declare minor1 before publishing their pending transaction.
Explicit minor0 remains available for legacy writing and rejects unsupported
startup data before raw finalization. Readers accept only the supported1.0/1.1
major/minor combinations with exact object keys, typed integer widths and stable
enum tokens. Root byte limits, depth12, event/key budgets and array rejection are
unchanged. No localization-dependent identifiers or floating-point timestamp
conversion are introduced. Prefix version is absent for raw-only jobs.

Version1.1 preserves all original startup admission identities, rates, channels,
frequency, native geometry, full-FIFO publication, source dispositions, partial
first-packet offset, original capture stamps and as-of time. It also stores native
warmup limits, independent pre-lease times, attempted/committed counts, first/last
SDK clocks and acquisition/optional release HRESULTs, content phase, budget stop,
context acquisition time and terminal startup phase. Startup fault is a stable
new token; old tokens retain their meaning.

A Complete result requires declaration/readiness consistency, the same prepared
handoff admission/publication rules, actual acquisition evidence, and original
successful SDK warmup/content/Stop facts. Warmup, startup, content, finite slack,
consumed PCM and native committed PCM remain distinct. Invalid and terminal native
originals remain diagnostics and are not normalized into healthy observations.
A complete legacy mode cannot contain startup SDK facts. These checks validate
stored consistency; a file itself cannot prove physical execution or timing.

This changes the native auxiliary journal only. Raw media/journals and general
project schemas remain unchanged. Older native-journal readers that support only
1.0 deliberately reject1.1 instead of dropping unknown evidence.

## Explicit migration and recovery

migrateNativeOriginalRecording acquires the same exclusive inactive recording job
lease used for raw verification and native outcome publication. It verifies the
common raw prefix and raw-journal hash before replacing a1.0 native journal with
1.1. The original raw bytes, finalized/partial status, copied SDK/fault metadata
and outcome disposition are preserved. No readiness, warmup or successful native
completion is inferred from old state. An already1.1 transaction returns without
rewriting or invoking publication callbacks.

Pending legacy transactions can migrate and receive copied joined outcomes later.
Raw-only, active, mismatched, corrupt or unsupported jobs cannot acquire fabricated
native evidence through migration. Before/after migration publication boundaries
are explicit and retain existing boundary numbers. Publication remains atomic;
a later exception does not erase the newly published original result.

## Acceptance

The new composed persistence fixture creates the pending declaration before the
simulated native Start, appends original raw views outside every RT guard, then
saves and independently reopens actual prepared-processing/render results.
Mono/stereo44.1/48/96k and failed Stop runs preserve the complete immutable ready
receipt, original warmup leases, acquired time, finite output, raw/stamp hashes
and SDK outcomes.24 malformed new Complete fields are refused.

Seven terminal workflows preserve zero-frame cancellation, cancellation after
content, Startup fault, live underflow with consumed PCM differing from native
commitment, missing SDK end, failed warmup acquisition and nonfinalized partial
raw recovery. No capture SDK end is invented for these diagnostic workflows.
Zero-frame and partial runs report against the verified common prefix; they do
not relax the existing nonempty raw-finalization requirement.

Migration fixtures cover all preceding outcome families, active/exclusive lease
and resource refusal, unknown/negative/noninteger versions, foreign identity,
idempotence and later partial reporting. Legacy outcomes compare as exact JSON
objects after removing only the newly absent fields. Four actual native-writer
process interruption boundaries and two actual migration process boundaries
verify before/after publication recovery. Child processes use simulated SDK data.

Development warnings from adjacent fixture branches were corrected by formatting.
The first diagnostic attempt placed the new routine in a capture thread and tried
to finalize an empty take. The original failed log is retained locally; the routine
now runs on the synthetic control owner and zero-frame reporting preserves the
existing invariant. No production finalization rule or deadline was relaxed.

## Exact-source qualification

Sourcefd1826d96f34a01c3584b0bdfb6a4c0c138ce6ae, tree
4f6904edf7e6f7703e2205ec01cadc870aa93988.

Six selected suites pass in each profile:

| Profile | Time | Native outcome checks | Composed checks | PCM error | Audited allocation/free/lock |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release | 0.67s | 1,044 | 2,475,990 | 0 | 0 |
| ASan/UBSan | 6.88s | 1,077 | 2,475,990 | 0 | 0 |
| TSan | 6.72s | 1,059 | 2,475,990 | 0 | 0 |

Native check counts vary with actual child-process wait observations. Seven
composed persistence runs, seven diagnostic persistence runs and two actual
migration process boundaries execute in every profile. The preceding concurrent
capture/processing/render owner audit still passes; disk work remains outside
callback guards. Audit interposition has limited syscall/dependency coverage.

The [state qualification packet](../tests/results/M2/2026-10-11-native-state11/summary.json)
binds634 exact Git/checkout inputs,18 Linux executable hashes, nine Linux objects,
five Windows objects, commands/configurations and all full original logs.
Original/compressed hashes are retained; no executables, objects or media are
included. Existing warnings from unchanged mixing sources are retained with exact
preceding source blobs and system-header hashes; no flags are suppressed or
warnings classified as false positives.

Windows SDK adapter and four test targets compile/link. Five affected objects
compile without warnings with the persistence fixture explicitly enabled. No
Windows runtime or hardware qualification is implied by compilation.

Ordinary inheritance of protected MAINc5e15a1 produced868e3a4 with the whole
qualified source tree unchanged. Three merge conflicts were resolved to the
complete frozen source, retaining all original stages and conflict text.
Builds were sequential, single-job, low priority and preserved preceding qualified
binaries.

## Next implementation task

Fresh native Windows CI must independently execute the new1.1, composed persistence
and migration process workflows. Compilation alone is not execution evidence.
The factory remains disabled; no installed/native physical audio, latency, room
correction printing, EQmix or recording alignment gate is claimed.

Next compose a finite owned native monitoring/recording factory using the already
prepared capture, original clock, full-FIFO readiness, native warmup, processing,
raw writer and versioned outcome routes. Include explicit mono-input to stereo-monitor
routing: current prepared processing and readiness validation require matching
channel counts, so different endpoint layouts remain a concrete gap.
Declare actual endpoint geometry before
activation, admit/retire all owners explicitly, preserve raw takes and all original
failures, and qualify synthetic/owned-route Stop, EOF, timeout, device loss and
interrupted recovery before adding normal UI activation. The full suite remains
active and incomplete.
