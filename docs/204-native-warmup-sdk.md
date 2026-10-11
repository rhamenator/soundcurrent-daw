# Finite native-only warmup leases

2026-10-11. M2 progress. Full frozen functional, quality, content and compatibility
scope remains open, including M2–M11, imports, equipment profiles, installation
and European localization.

## Preceding readiness integration

PR139 passed all four required jobs on38155844567: native MSVC70 suites179.80s,
the new startup suite and three original20-take/five-second diagnostic completions.
Head `b2bb94b9f46d10f9046e24f1e45cb0d688c23270`, base
`e926e275c22b169cb8829e8bdaf396d7c8e96e8c` and tested merge
`c7ed64579a92119f569553ed5766c07aa30e61dd` were bound to all four original full
job logs, exact tree and14 reviewed blobs. Latest required checks, enforced
protection, clean head and resolved threads were checked before an ordinary
matching-head squash. Protected MAIN `31db463ba36b568ce83e52a37476f228d9ca3afd`
matches tested tree `f9340b5650927d945d4b03e7a0ab657a9bf6e7f6`, base parent and
all14 blobs. API and remote refs were verified after merging.
[Protected receipt](../tests/results/M2/2026-10-11-monitor-ready-protected-main/summary.json)
retains all four full UTF-8 logs byte for byte, including byte-order marks,
qualification/admission/merge bindings and actual diagnostic completion markers.
Original Windows timeout and interrupted-parent causes remain unresolved; later
greens are bounded non-reproduction. These jobs did not execute the new SDK hook.

## Optional SDK hook

The native render callback table now accepts a pre-lease Content, NativeWarmup or
Abort decision. A hook requires explicit finite frame and QPC limits; mismatched
hook/limit declarations are refused before creating the native owner. Existing
callbacks without a hook retain their behavior.

The SDK owner obtains the original `IAudioClock` position/timestamp and a separate
actual current QPC reading before asking for a lease. The prepared portable
lifecycle admits native geometry, clock frequency, monotone current QPC, elapsed
wait and remaining warmup frames. Abort, Invalid, Timeout and FrameBudget remain
distinct terminal causes. One warmup receipt can be outstanding. Content latches
the live phase; later warmup cannot disguise a live underflow.

NativeWarmup calls actual SDK GetBuffer and SILENT ReleaseBuffer, followed by the
existing client Start path after priming. It never calls the PCM fill callback,
reads/writes native backing or advances engine/project positions. The SDK silence
flag removes the need to write silence samples; see [ReleaseBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiorenderclient-releasebuffer). Queue sequence
includes warmup; content-submitted position excludes startup and warmup. Successful
warmup frames and attempted leases are separate from callback content, startup and
end guard. First/last original warmup clocks, current QPC and acquisition/release
HRESULTs are retained. Failed acquisition has no invented release; failed release
has no invented commit. An unreported outstanding receipt makes the end invalid.
Actual Start, Stop, native failure and original end reporting remain in the SDK
owner. The portable fixture uses simulated SDK results, not physical endpoints.

## Versioned-state boundary and pending consumption

Auxiliary native schema1.0 cannot represent these new observations. Its encoder
explicitly refuses any warmup/pre-lease evidence instead of dropping it. Ten
refusal cases leave the original pending transaction unchanged; the same writer
can subsequently finalize its valid original1.0 result. Old1.0 reads/encodes and
raw media/journal formats remain unchanged. Explicit1.1 migration is still needed.

The monitor context/binding does not yet consume readiness through this hook.
The native factory remains disabled. This is a dependency for the composed native
workflow, not evidence that monitoring warmup is usable or qualified on hardware.

## Exact-source qualification

Source `fa1d55f3b0148ae035c4c5b0081a629352ee78c1`, tree
`624e0e6699efcc25fc6c6ef0e2a9f8ff42786634`, passes four selected suites per profile:

| Profile | Suites | Time | Lifecycle checks | Startup checks | Guarded allocation/free/lock |
| --- | ---: | ---: | ---: | ---: | ---: |
| Debug | 4 | 3.86s | 77 | 19,698 | 0 |
| Release | 4 | 1.14s | 77 | 19,698 | 0 |
| ASan/UBSan | 4 | 11.78s | 77 | 19,698 | 0 |
| TSan | 4 | 18.24s | 77 | 19,698 | 0 |

Tests preserve separate actual-result domains, wide clocks/nonzero success codes,
failed acquisition/release, expiry before content, frame exhaustion, backward or
foreign clocks, pending receipts, Abort and refusal to return to warmup after Live.
Original startup fixtures retain six concurrent reader cases and direct PCM error0;
native outcomes retain four actual process-interruption boundaries. Native-outcome
and render fixture check totals vary with owner scheduling; full logs retain each
actual total. They are not asserted to be equal across profiles.

Windows SDK and three test targets compile/link; five modified source/test objects
compile without warnings. No new native MSVC execution, SDK activation, installed
audio or professional-latency claim follows from cross-compilation.
[Hook evidence](../tests/results/M2/2026-10-11-native-warmup-sdk/summary.json)
binds595 selected Git/checkout inputs,19 executable hashes, five Windows object
hashes, configurations, commands and full logs. No executables or media are included.
Original QPC-wrapper comparison compiler failure is retained; scalar comparison
fixes that type error. Existing GCC15 warnings in unchanged mixing sources remain
in the logs, with exact protected-MAIN provenance; flags were not suppressed.
An ordinary protected-MAIN inheritance produced no conflicts and preserved the
entire qualified source tree. Builds were sequential, single-job and low priority.
No VM or physical audio route was used.

## Next implementation task

The initial implementation bounded QPC frequency to1GHz to protect multiplication.
That restriction is removed in source `b5f2a29cbf4694c1e6e9dead39f8496c60b0a5d6`.
Windows exposes the actual boot-fixed frequency; it must not be inferred from CPU
frequency or a fixed10MHz assumption. See [Microsoft QPC guidance](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).

The follow-up converts ticks to100ns with checked64-bit arithmetic. Ordinary
frequencies use a checked multiply/divide; larger frequencies use exactly24 bounded
modular steps. No floating point, allocation, loop over elapsed time or compiler-
specific128-bit type is needed. Zero frequency and an unrepresentable result are
refused. This scales the independent current-QPC wait clock; original SDK audio-
clock fields are unchanged.

Eighty literal oracles were generated using independent Python arbitrary-precision
multiply/divide, including low/high frequencies,3GHz, the fast/fallback boundary,
zero frequency, maximum ticks/frequency and result-overflow edges. The four suites
pass Release1.14s, ASan/UBSan11.58s and TSan17.69s, with157 lifecycle checks per
profile and zero guarded allocation/free/lock. The earlier Debug evidence remains
explicitly bound to initial source; the follow-up does not claim a new Debug run.
Five Windows source/test objects again compile without warnings; SDK and three
Windows test targets compile/link. [Counter follow-up](../tests/results/M2/2026-10-11-native-warmup-qpc/summary.json)
binds595 inputs,15 executable hashes, five Windows objects, exact-source delta,
commands, configurations and full logs. Original initial-source proof remains
separate. Boundary/representative conversion checks do not constitute native
activation, installed playback or full device qualification.

Connect the optional monitor binding/context hook to the immutable startup receipt.
Admit exact native format/run/stream identities and matching wait/frame budgets.
Publish original render readings once per lease, including warmup; avoid publishing
again during the corresponding Content fill. Acquire readiness only after actual
full-FIFO publication, retain the original receipt and refuse cancellation, short
input, faults, stale identities and clock-queue pressure before PCM consumption.
Keep actual SDK outcome validation and joined retirement explicit.

Then migrate auxiliary native state1.0 to1.1 for warmup, readiness, original source
dispositions and failure clocks, retaining exact raw-journal/hash binding and
inactive exclusive leases. Old1.0 reads must not invent observations. Qualify
composed original-clock/full-FIFO PCM, SDK failures, Stop/EOF/owner loss and recovery.
Independent native/installed Linux and Windows, professional latency, full EQ/mix,
alignment and punch/loop gates remain required before enabling the native factory.
