# Coordinated desktop execution memory and recording payloads

X006 checkpoint, 2026-10-07. Extends [prepared execution ownership](85-graph-memory-resources.md).

## Product behavior

New desktop playback, fixed/manual recording and export preparations sample the
trusted Project resources parent limit. Their local graph/capture/render allowance
uses that limit; the shared ledger separately enforces the combined declared
payload of all retained owners. Raising the parent can admit a larger generation;
it does not reset a running graph, change a device, increase its quantum or
promise a callback deadline. Project files cannot raise trusted policy. Standalone
core callers retain their explicit local limits and optional parent configuration.

Recording bridges use immutable prepared DSP/pool/reader/cache declarations instead
of treating the graph's entire allowance as occupied memory. This avoids reserving
a mostly unused allowance while still checking run plus bridge plus outstanding
capture banks against the local envelope. They never sample mutable reader/cache
statistics concurrently. Explicit child ledgers remain supported.

## New ownership

| Owner | Declared payload | Credit release |
|---|---|---|
| Desktop playback receipts | Dynamically prepared per-lane applied/required revision arrays | Worker-side Stop/bundle completion; no fixed 256-track array |
| CapturePipe | Object plus every prepared float slab; optional parent reserved before pools | Off-RT pipe destruction after callbacks/consumers join |
| DuplexBridge | Lane bindings, pointer arrays, observations and metadata allowance | Quiescent bridge destruction; each capture pipe has its own lease |
| ManualPunchBridge | Arm bindings, pointer arrays and bridge metadata | Quiescent bridge destruction |
| ManualPunchTake | Aggregate take metadata, all lane pools and bindings | After retirement/command acknowledgement and consumer join; nested pipes are not charged twice |
| CaptureWriter | 128 KiB declared hash/journal/writer workspace plus conservative native-path weight | Disk/control owner destruction after files close and worker join |
| PipeWire monitoring-off scratch | Prepared discarded output floats | Native owner destruction after callback and writer/reader shutdown |

Fixed/manual owners inherit the execution parent for writer workspace unless the
caller explicitly supplies a separate writer scope. Writer refusal precedes job
creation. Take-bank refusal precedes capture-pool allocation and slot/identity
publication. Constructor failures return leases; existing owners remain usable.
Audio paths only use the already prepared pools/queues. No ledger mutex, reserve,
release, allocation, logging or disk IO is added to callbacks.

## Acceptance contract

Qualify full-parent refusal and retry, exact owner counts and lifetimes, raw capture
and finalized WAV values, job creation refusal, writer cancellation/failure unwind,
manual outstanding-bank overlap/refusal/retry and control-side retirement. Exercise
an actual 512-track file/EQ generation through the Qt preparation seam with no
native audio device; full-parent refusal must leave the saved project intact.
Verify the actual summed float samples/peak, explicit output-intent edit, exact
execution credit release on Stop, and explicit Save/reopen of the chosen route.
The terminal desktop meter intentionally returns to zero; it is not a stored
peak report.
Exercise 512-lane live parameter receipts, withheld-lane backpressure and canonical
reordering without an out-of-range acknowledgement array. Retain existing
timing/recording recovery/export/RT audits. Exact executed scopes,
source/executable hashes and original failures belong in the checkpoint receipt.

## Remaining gates

These are declared prepared payloads, not full allocator/RSS or CPU admission.
Copied owner Sessions, parser/recovery/discovery/other IO, library/Qt internals,
bulk parameter preparation/cancellation responsiveness, expanding trials,
descriptor/process overhead and all transient metadata remain
outside this scope. Windows core cross-building is not Windows runtime qualification.
No physical audio or VM test is required for this checkpoint. Native observation71
and its clock/source CPU cause remain unresolved.

The recording adapters still bound 256 armed lanes/packed native input channels;
this is a separate implementation/backend gap, not a product project-track ceiling.
Paged GUI inventories beyond Qt index capacity, freeze/bounce, multi-worker
scheduling and sustained modest/strong Linux/Windows workload profiles remain
required. All frozen functional/quality/content/native-compatibility contracts,
X004/X005 and European language qualification remain open. X006 is incomplete.

Next: charge remaining copied owner state and IO/transient work, measure actual
allocation/RSS envelopes, then scale recording arm inventories independently of
backend ports and qualify freeze/bounce and scheduling.
