# Owned PipeWire recording and monitoring workers

Checkpoint229 adds the owned route and lifecycle factory: native capture/render
owners, one shared processing worker, and one serialized raw/receipt disk worker.
Controller/Qt integration and durable route/terminal state remain required. This
is not full workflow parity or installed/physical qualification.

## Preparation and role ownership

The route admits expected capture/render graph geometry, immutable EQ/channel
mapping, startup/stop/control/clock/receipt handoffs and a conservative aggregate
payload lease before allocating banks. Expected formats are not claims about
already negotiated hardware: actual callbacks validate rate/quantum and retain
native mismatch faults. Different widths require explicit mapping. No rate,
quantum, default device or driver is changed.

A stream factory constructs inactive native endpoints with route references.
The genuine Linux implementation uses the existing PipeWire filters. Ports are
selected explicitly before activation; partial route-selection failure closes the
prepared owner. Independent current CLOCK_MONOTONIC sampling stays distinct from
original graph timestamps. Native callbacks never write files or wait on workers.

Activation starts disk and processing workers before render/capture activation.
Partial construction/start failures and destruction stop/join all native callbacks
and workers before component retirement. Progress copies atomic counters only.
Mutable DSP statistics and complete native outcomes are acquired only after their
owner ends/joins. Missing capture end publishes OwnerLost after native capture
join; it does not fabricate a native or WASAPI result.

## Processing, paired storage and finite Stop

The processing worker pumps original clocks/control and performs bounded in-process
EQ, mapping and conversion. Scheduling waits happen outside those bounded methods.
The disk worker consumes a capture receipt and its corresponding raw-bank view,
retains that view through append, and writes render receipts independently. Receipt
publication follows raw publication, so a ready receipt must have its raw packet.
Failed writes/leases/metadata lanes stop the owner and preserve the last paired
checkpoint; queued unpaired raw data is never certified as committed media.

The control service joins capture after a processing horizon request, then closes
capture receipt EOF. Render joins after actual native finite-content return (or
cancel/fault); rendering receipt EOF follows join. Both workers finish before route
settlement and stored-prefix finalization/inspection. Cancellation, activation
failure, storage failure and native/processing faults remain separate.

`Recorded` means a verified stored paired prefix and healthy joined native facts
with matching monitor extents. It is not audible hardware drain, verified recording
alignment, automatically attached playable media, or durable terminal metadata.
The current typed result carries original native first/last/fault facts, startup,
stop requests, worker/native/storage failures and recovery outcomes; route/terminal
persistence still needs a versioned schema.

## Qualification

Engine implementation **a764263** binds821 source/checkout inputs; current
**b240e02** adds bounded CI retention/self-check only and binds823 inputs.
All814 non-CI/PowerShell engine inputs remain identical. Four Release suites
pass2.47s, four ASan/UBSan3.49s with leak detection disabled; five Windows
owner/route/receipt/stop/fixture objects compile warning-free. The complete
owned four-role ThreadSanitizer test passes2.62s without suppressions, including
normal finite processing, cancellation, activation/rate/disk failures and join
retirement. No GUI or opaque-framework TSan qualification is claimed.
See [the exact qualification packet](../tests/results/M2/2026-10-12-pipewire-owned-recording/summary.json).

The concurrent fixture runs actual separate capture/render-role threads plus the
processing and disk workers, with simulated native clock/return facts. It checks
finite2,400-frame peaking-EQ/mapped output, exact raw headroom bits, applied Stop,
payload credit retirement and normal joins. It also checks prepared cancellation,
render/capture activation failure, actual callback rate mismatch and injected disk
failure before paired commit. This is not native SDK qualification by itself.

The genuine private PipeWire1.6.2 run captures99,072 raw frames bit-for-bit, processes
8,192 monitor frames and observes all8,192 in an owned sink. It verifies -6dB peaking
response at1kHz, preserved DC/headroom and explicit mono-to-stereo weights(1,-.5),
paired journal reopening,387 capture and408 render receipts, callback allocation/
free/lock audit hits0, and joined native/processing/disk/daemon owners. The owner-only
socket uses a dummy driver and test nodes only; no physical devices/session manager
or user playback/default/volume changes. The sanitizer private run also passes:
100,352 raw frames,8,192 monitor/sink frames,392 capture/412 render receipts and
audited callback hits0. Exact counts differ with scheduling; that is not a coverage
percentage.

Initial development warnings are retained and corrected by separating statements;
a native fixture initially lacked its thread header and that compile failure is
also retained/corrected. No validation, buffer, deadline or diagnostic threshold
is relaxed. Opaque framework/GUI ownership and earlier native contention failures
remain unresolved.

## Original PR163 failure retained

PR163/run38313719137: Linux151/433.20s, Windows desktop20/75.64s and core cross-build
pass. Windows native unit selection78/286.12s passes, including new startup/stop
checks; the overall native job fails its additional concurrent-owner diagnostic.
Attempts1/2 complete20 takes; attempt3 fails take6 at submit-out, identity7/status12.
The original snapshot shows full8,192-frame capacity,31 ready slabs plus256 acquired
frames and write-in-flight; observation age17.0418ms, completed write maximum23.9181ms.
This proves pool pressure/refusal, not the underlying OS/I/O/scheduling cause.
Original logs, source bindings, selected artifacts and the failure
filesystem inventory are retained (local ZIP/audio only). The diagnostic printed
a preserved runner-temp project path; that project is not included in the uploaded
archive, so its actual failed take bytes are unavailable after runner retirement. No unchanged rerun replaces this failure and the original
20-take/5-second policy stays intact. New CI now validates and copies up to three
generated immediate-temp owner projects, with entry/byte bounds, reparse/path
refusal and before/copy/after byte hashes. The snapshot is explicitly non-atomic.
Local PowerShell checks Unicode/audio bytes, foreign-path and symlink refusal;
Windows execution is pending in the next CI run. Earlier startup/pool/graph-gap/GUI reports also
remain open.

## Next implementation task

Persist declared routes, initial EQ/channel map and joined native/processing terminal
and fault outcomes with strict versioned state. Add interrupted publication/recovery
binding to the current paired journal, then integrate the owner in the native
controller/Qt recording workflow with explicit user placement. Qualify both platforms,
installed/physical behavior, hotplug and processing quality independently. Continue
causal investigation of the original pool-pressure failure without weakening its gate.
The full later milestones and reviewed European localization remain required.
