# Prepared monitor output and original native render end

Checkpoint198 implements the inactive WASAPI monitor render binding, output-bank
consumer and original render-clock handoff. It is original GPL-3.0-only C++20,
with no new dependency, driver, project schema or GUI activation. The native
duplex factory remains disabled until startup, durable native-end recording state
and actual SDK/installed workflows are qualified.

## Preparation and ownership

The SDK stream supplies its actual immutable prepared rate, channels, capacity,
maximum fill, channel mask, clock frequency, timing and declared route identity.
Preparation checks bank geometry and fresh consumer state, clock route/generation
and bounds. Binding requires exact descriptor equality, including the channel
mask. Frequency need not equal sample rate. No layout conversion, project/capture
mapping or clock fit is inferred from descriptor equality.

A stable callback table exists before SDK stream construction. One serialized
control owner binds while one SDK owner may exit. Release/acquire transfer makes
the context visible only after binding wins; an earlier unbound SDK end retains
its original receipt and refuses activation. The external context, bank and
clock transport outlive all owners and joins. The wrapper joins its SDK stream
before retiring the table; repeated joined stop is inert. Control cancellation
sets atomic intent and signals the existing OS event without joining in callbacks.

The context and binding reserve their whole fixed object sizes against optional
resource credit before use. Preparation/retirement remain off RT. This is not
admission of the entire native FIFO, processing graph or route resource envelope.

## A real lease is distinct from committed or played audio

Each callback retains scalar original clock/lease metadata, with no SDK backing
pointer. Requested frames, bank-consumed monitor frames, successful native
content commitment, certified final slack and the exclusive bank end are separate
quantities. The exclusive end is a monitor ordinal, never a project position or
hardware clock. Original unsigned position/QPC/frequency are handed off before
bank consumption. Queue pressure cannot erase an unconsumed output prefix.
Missing QPC remains zero with Missing reliability; it is not reconstructed.

A full healthy live pull continues. A partial live pull signals actual underflow
and aborts the native lease, leaving the unused suffix untouched. Producer faults
refuse diagnostic-prefix commitment. Cancellation or a deliberate control Stop
aborts without inventing a clock/underflow fault. No retry, spin or ordinary
underflow silence is introduced.

Only healthy producer completion plus a drained bank permits zeroing the unused
suffix of the final lease. Content Finish allows the SDK to continue its admitted
guard/drain path; it does not request immediate Stop. The context has no result
or render EOF until the original SDK-owner end is received after Stop/exit.

Complete requires matching descriptor, original last clock/lease metadata and
callback counts, successful native commitment, actual Start, admitted boundary
silence, observed zero padding/drain, final Finish and a successful actual Stop.
Failed release, failed Stop after observed drain, first unavailable HRESULT,
owner exception, invalid receipt, clean undrained interruption and missing joined
owner remain distinct. Failed release can consume bank PCM while committing zero
native frames; this does not become a successful recording or playback claim.

The first immutable context result is published before render-clock EOF. Duplicate
end and late callbacks cannot rewrite it or consume further PCM. A late protocol
fault can remain separate metadata. A missing joined SDK end yields OwnerLost
with no fabricated SDK receipt or HRESULT. `cancelRequested` records this context's
own atomic intent; peer bank/clock cancellation can classify Cancelled without
changing that flag. In-flight cancellation and native completion can race; no
unconditional cancellation-wins policy is claimed.

## Frozen component and composed evidence

Source `0a36c814afe5e3b9954afc350e578f0a3f3b35a1`, tree
`43f6a49cd013377bd9f31ade594b469bd4781f66`, passes eight suites per profile:

| Profile | Seconds | Observed render-context checks |
|---|---:|---:|
| Release | 0.47 | 18,976 |
| ASan/UBSan | 2.67 | 19,252 |
| ThreadSanitizer | 4.09 | 19,291 |

Observed counts vary with the winners of 1,000 actual control/SDK binding races.
The prepared fixture checks finite 0/1/7/8/15/24-frame ends, canaries, bit-exact
float headroom, original wide/missing clocks, underflow, clock pressure,
producer/cancel/Stop failures, failed native release, failed Stop after drain,
undrained end, descriptor mismatch, resource refusal/retirement and late callbacks.
A separate three-owner phased run supplies 1,000 output leases: 7,999 monitor
frames, 8,000 successfully committed simulated native frames and one final slack
frame, with 1,000 unchanged render clock readings consumed by the producer owner.

Four additional composed capture/processing/disk/render cases request 0/7/512/3,072
monitor frames. Original recorded raw extents are 64/512/960/3,520 frames, retaining
actual postroll and exact disk sample hashes. The production capture/render
contexts and processing kernel participate; an independent stopped-kernel oracle
checks output PCM. Original capture Stop and render drain/Stop receipts remain
separate. The composed source rate is synthetic 64 kHz, the initial clock fit is
explicitly synthetic, and simulated render pacing waits for a complete bank block.
This is not an SDK startup, deadline, hardware latency or full EQ/mix workflow test.
The capture-Stop fixture passes 11,881 checks per profile. All guarded C++ and
interposed libc allocation/free/blocking-lock audits remain zero.

The [64,059-byte/17-member packet](../tests/results/M2/2026-10-11-native-monitor-render-context/summary.json)
binds 588 selected Git/checkout inputs, 24 Linux executable hashes, 12 Windows
object/library hashes, complete build/CTest/LastTest logs, configurations and
qualification script. The actual SDK wrapper and complete core-only Windows
target set compile. This packet contains no Windows execution of the new context,
SDK device runtime, installed qualification, executable or media upload.

## Independent preceding Windows execution and protected MAIN

Checkpoint197 independently passed MSVC job114469974987/run38139406261:
67 suites/144.24s, including render lifecycle/0.01s. Its
[48,150-byte/three-member packet](../tests/results/M2/2026-10-11-native-render-end-windows/summary.json)
retains the complete original native job/API, 17 reviewed blobs and tested
merge/tree/parents. All five changed C++ files match frozen4d77622. All four
original stacked jobs passed and their byte-exact logs remain local. This is
portable lifecycle execution, not new context execution or refreshed MAIN admission.

PR132's ordinary protected squash `c828bd44b08b34bb02f82c5baf8a4bb202baba26`
matches its whole tested tree, base parent and 28 reviewed blobs from
run38138835329. The
[145,457-byte/eight-member packet](../tests/results/M2/2026-10-11-capture-stop-current-main/summary.json)
retains all four complete byte-exact jobs, review/admission/post-merge receipts
and original UTF-8 BOMs. No protection change or admin bypass occurred. PR133
inherits this MAIN at2b5fe08, preserving the original Windows owner diagnostics;
its fresh current-MAIN cohort is pending. Older stacked greens cannot admit it.
Original Windows manual-group and parent-interruption failure causes stay open.

## Next task and activation gates

Persist original capture/render ends, consumption/commitment/slack distinctions
and failure status in versioned recording transaction state, including interrupted
recovery. A context result currently survives only in prepared run memory; do not
classify durable raw PCM as a successfully completed monitoring run without its
original SDK outcome. Then implement and qualify explicit native startup admission.

Startup remains substantive: drift confidence needs a minimum observation span,
render-clock movement needs actual Start, while output needs ready processing.
The current steady processing occupancy and bank capacity cannot be assumed to
hold that startup interval or the entire native priming FIFO. Exact chunk-bound
admission alone does not solve this cycle. Establish admitted warmup/prefill,
capacity, clock quality and retained raw pre-roll/dispositions without fabricated
clock readings, weakened confidence or disguised deadline gaps. Actual SDK and
installed short/long Stop, cancellation, device loss and join/retirement need
independent Linux/Windows evidence before factory activation.

Raw-to-project alignment, playable finite-prefix admission, repeated punch/loop,
EQ/mix processing quality, European localization and full M2–M11/frozen-reference
functional, quality, content and compatibility scope remain open. See
[ADR130](decisions/130-native-monitor-render-context.md).
