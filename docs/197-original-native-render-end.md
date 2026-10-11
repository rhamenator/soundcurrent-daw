# Original render lease releases and native end

The existing WASAPI renderer now reports an immutable prepared descriptor and
one original SDK-owner end. Its prepared bookkeeping distinguishes callback
requests, successful content releases, native startup/guard silence, actual
empty-queue observation and the SDK Stop result. This is original GPL-3.0-only
C++20 with no new dependency, driver, schema or GUI activation. The duplex
factory remains disabled; the monitor-bank render context is still the next
implementation gate.

## Actual SDK integration

Successful preparation exposes rate, channels, actual buffer capacity, maximum
fill chunk, mask, actual SDK clock frequency, timing and declared route identity.
Maximum fill is the smaller of actual capacity and the configured chunk bound.
Clock frequency is not assumed equal to sample rate. The descriptor is immutable
after preparation; legacy callers may retain zero declared identity.

`PreparedWasapiRenderLifecycle` exists before the SDK owner starts. Once native
preparation succeeds, the owner installs the descriptor and a scope guard reports
its original end after Stop/exit and before native retirement/join. Optional
callbacks append to the existing callback table, preserving three-field callers.
Control reads `endAfterJoin()` only after joining; an unjoined or unreported end
returns nullopt rather than an invented successful empty receipt.

The SDK owner supplies the actual content lease clock, requested/released extent,
callback action and presence, and ReleaseBuffer HRESULT after each actual release.
Abort releases zero frames. Failed release does not increment committed content;
callback-requested frames remain separate. Nonzero successful HRESULTs are
retained. Native-only startup/guard leases have separate original results and
committed counters, with no content callback or monitor/project advance.

Only a successful GetCurrentPadding observation updates the original padding
record. The existing finishing path records queue drain when padding is zero
and all admitted guard frames were committed. A subsequent failed SDK Stop
preserves that observed empty queue **and** the failed HRESULT; it cannot become
a successful native drain. An unstarted exit contains no fabricated Stop call.
The first failure/operation and owner exception remain separate original facts.
The existing `drained()` API still requires normal queue drain and successful
actual Stop. Queue drain is not a measurement of acoustic or amplifier behavior.

Optional stop polling checks at loop entry, each content/guard lease and before
Start. At most one lease can race a check. A content Finish awaiting native
drain must not itself make `shouldStop` true; cancellation/fault is a different
operation. An inactive owner still needs a control event to wake its initial
wait. Active waits retain the existing two-second timeout; this is not a new
wall-clock Stop guarantee. Event signaling and joins remain off RT.

## Receipt and ownership limits

The bounded record retains the last content/silence release, not an unlimited
lease trace or PCM pointer. Callback-requested frames, successfully committed
full native content leases, startup/guard and padding are distinct domains.
Committed content includes certified terminal slack supplied by a future monitor
context, so it must not be used directly as an exclusive monitor/project end.
Native clock positions/QPC remain unsigned and unchanged, including values above
signed-64-bit and exact-double ranges.

One SDK owner updates this object; joined control reads it afterward. Prepared
methods use fixed POD bookkeeping and checked accumulation, without SDK calls,
allocation/free, blocking locks, I/O or logging. The end callback context outlives
the SDK join. Duplicate ends and post-end updates are inert. Geometry, boundary
extent, drain prerequisite and Start/Stop-presence inconsistencies mark the
receipt invalid while retaining original inputs. This is not a comprehensive
SDK sequence, hardware timing or raw-to-project validator.

## Frozen evidence

Source `4d776225fb07509749c561c97fa130cdb458b665`, tree
`4ba24651f5b2c6d1695e9a827fdb466c78e2018c`, passed seven suites per profile:

| Profile | Seconds | Render lifecycle checks |
|---|---:|---:|
| Release | 0.43 | 36 |
| ASan/UBSan | 2.37 | 36 |
| ThreadSanitizer | 3.94 | 36 |

Tests preserve failed content/startup/guard release, callback Abort, absent
callback backing, failed Start, failed Stop after empty queue, owner exception,
duplicate/end-before-join behavior, original wide clocks and successful nonzero
HRESULTs. A separate actual source thread supplies 20,001 synthetic content
lease results totaling 1,280,007 requested/committed native frames; control reads
only after joining. These are bookkeeping inputs, not PCM playback or a native
queue/deadline experiment. The six prior capture/control/processing suites also
pass; guarded C++ and interposed libc allocate/free/blocking-lock audits are zero.

The [62,949-byte/17-member packet](../tests/results/M2/2026-10-11-native-render-end/summary.json)
binds 584 selected Git/checkout inputs, 21 Linux executable and nine Windows
object/library hashes, full build/CTest/LastTest logs, configurations and script.
The modified actual SDK renderer and complete core-only Windows target set
compile. No Windows execution of this new render lifecycle, SDK runtime,
installed preview, executable or media is claimed by this packet.

## Earlier capture execution and protected MAIN

The preceding capture-context source independently passed MSVC job114465301918,
run38137846669: 66 suites/147.13s, including binding/0.07s. All four original
stacked jobs passed with their complete checkout-bound logs retained locally.
The [48,090-byte/three-member native packet](../tests/results/M2/2026-10-11-native-capture-context-windows/summary.json)
retains the full original job/API, 21 reviewed blobs and tested merge/tree/parents;
all seven changed C++ files match frozen67b7004. This tests Checkpoint196 portable
callbacks and races, not this render lifecycle or actual SDK device execution.

PR131's ordinary protected squash `4342ec6ca465c03316115c4d20762962f287da79`
matches the whole tested tree, base parent and 17 reviewed blobs from
run38137484653. The [142,848-byte/eight-member packet](../tests/results/M2/2026-10-11-monitor-control-current-main/summary.json)
preserves all four complete byte-exact jobs, review/admission/post-merge receipts
and original UTF-8 BOMs. No protection change or admin bypass occurred. Capture
PR132 now inherits this MAIN and requires its own fresh cohort; prior stacked
success is not its admission evidence. Earlier Windows owner-failure causes
remain unresolved despite these fresh successes.

## Next task

Bind a prepared monitor render context after actual SDK descriptor admission.
Consume the output bank within the fixed native lease, refuse deadline underflow,
distinguish a healthy finite end from fault/cancel, and zero only certified final
slack. Preserve original render clocks and distinguish bank consumption from
SDK commitment and successful drain/Stop. Then persist original capture/render
ends in the recording transaction and qualify SDK/installed short/long Stop,
cancellation, device loss and retirement before activating the factory.

Initial clock confidence and startup capacity/pacing remain a separate real
admission gate; synthetic prior fits cannot qualify them. Alignment, playable
finite-prefix admission, repeated punch/loop, EQ/mix execution, processing quality,
European language qualification and full M2–M11 scope remain open. See
[ADR129](decisions/129-original-native-render-end.md).

[Checkpoint198](198-native-monitor-render-context.md) now implements the prepared
monitor output context/binding and compiled SDK wrapper, with explicit finite
slack and original clock/end evidence. It also retains independent preceding
Windows197 execution: MSVC114469974987/run38139406261 passed67 suites144.24s,
render lifecycle0.01s; the48,150-byte/three-member packet binds17 blobs/full job.
Durable original SDK-end state, live startup and actual SDK/installed qualification
remain gates. This new context's Windows execution is not yet claimed.
