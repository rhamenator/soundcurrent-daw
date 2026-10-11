# Original capture/render clock handoffs

`PreparedMonitorClockHandoff` provides two bounded original-reading routes to
`PreparedMonitorProcessing`. This is original GPL-3.0-only C++20 code with no new
dependency, driver or license. It preserves native capture frame positions,
render tick positions/frequency, QPC100ns timestamps, reliability and
discontinuity fields. It performs no SDK calls, fitting or extrapolation.

## Admission, ownership and timing

Prepare the handoff off the real-time path. Admit matching nonzero identities,
render tick frequency, run generation, configured1–64 slots and a local record
sequence with a reserved terminal value. Reserve the whole inline owner through
the resource ledger before touching either fixed64-slot SPSC queue. Retirement
returns that credit after all owners join. This charge is a payload allowance,
not an allocator/SDK footprint measurement.

There are three owners: capture-clock producer, render-clock producer and
processing consumer. Each source has its own sequence and immutable first-fault
cell. The envelope generation/sequence describes the handoff; it is never a native
position, project timestamp or ordering between sources. Large native integers
and zero startup timestamps remain unchanged. Reliability enum and context are
validated here; timestamp motion, fit confidence and discontinuities are evaluated
by the existing controller. Missing or delayed readings do not refresh accurate
clock age. No atomic cross-source cohort or global publication order is claimed.

The optional handoff must outlive the processing owner. Constructor admission
checks identity, frequency and generation before the processing group allocates
its child buffers. `pumpClocks()` is processing-owner only: at most four capture
and four render readings per call. Terminal phase is checked before, during and
after pumping; Ready plus Fault is diagnostic and must never be fitted as healthy
data. `step()` independently checks terminal handoff state before progress, even
if pumping was skipped. Native adapters must read actual current QPC after
receiving those original timestamps, then pass that time to `step()`. Native SDK
publishing, clock acquisition and activation are not implemented by this slice.

Clock observation may refresh while output PCM is pending. Input, DSP and the
scheduled ratio grid remain blocked by output pressure. Original raw recording
positions and alignment remain independent of monitor output ordinals.

## Stop, cancellation and failure

Queue overflow and sequence exhaustion are terminal losses, not retry contracts.
Identity, frequency, reliability, producer/peer loss and consumer loss retain a
first receipt. An attempted original reading is present only for a real publish
attempt; abandonment or consumer loss does not fabricate one. An in-flight peer
publication may finish while a fault is becoming visible. Accepted prefixes are
retained diagnostically with fault phase, rather than fitted as healthy clocks.

Stop drains accepted records; cancellation or observed consumer loss refuses
new reads without changing their destination. An operation already in flight may
finish and reports its observed terminal phase. Consumer loss is visible even
after both producers finish. Control settlement is allowed only after all owners
join; abandoned producers fault, and cancel/consumer loss discards only queued
clock records with exact counts. Settlement is idempotent. Counts sampled during
publication/consumption are not coherent snapshots; joined counts are authoritative.
Prepared methods perform bounded POD/atomic/SPSC work without allocation, disk
I/O, logging or blocking locks.

## Acceptance evidence and limits

Frozen source `d0d4c6cddd1f8ba8e3565a4f51b75fc24ad1c686`,
tree `bc2baa84d5ddf43e7487209448b6c80cfc2482d3`, passed two suites in each
profile: Release0.29s, ASan/UBSan1.35s and TSan2.71s. Each profile performed
653 clock checks and451,522 processing checks. Guarded C++ and interposed libc
allocation/free/pthread-lock counts were zero.

The [60,597-byte/16-member packet](../tests/results/M2/2026-10-11-monitor-clock-handoff/summary.json)
binds568 selected inputs, six executable hashes and complete compilation,
CTest/LastTest/profile logs. A fresh Release compilation reproduced both tested
executable hashes. The first template-deduction compile failure and its repair
are retained; no runtime assertion was suppressed. Standalone member and frozen
Git/checkout byte checks passed. No executable or test media is included.

- Three actual concurrent clock owners transfer20,000 original readings per
  source, comparing values, order, generation and sequence. Producer credit
  waits/yields and a10-second fixture deadline are outside prepared methods.
  This is logical concurrency, not native pacing or deadline evidence.
- Admission/credit refusals, startup zero timestamps, missing/delayed readings,
  original discontinuities, native positions above2^63, QPC above2^54, scratch
  reuse, queue wrap, one-slot overflow and terminal sequence were tested.
- Stop/drain, cancel, producer abandonment, immutable fault, diagnostic prefix,
  late consumer loss after both producers finish, untouched refused-read
  destinations and exact joined accepted/delivered/discarded counts were tested.
- Six serial processor integration cases cover foreign generation refusal,
  bounded4+2 pumps, healthy512-frame output, overflow before healthy fitting,
  cancel/late-loss without pumping and stale accurate age after missing/delayed
  observations. Their1,024 original raw frames/prefix remain unchanged.

The processor clock integration is serial. Its earlier four-owner PCM test uses
a raw reader, not the disk writer. The clock concurrency test is separate from
that PCM fixture. This does not qualify a combined disk/clock/processor run,
advancing native time, SDK activation, installed preview, physical hardware, load
deadlines or full DAW parity. Independent Windows execution is recorded below. The
earlier [Windows processing observation](190-bounded-monitor-processing.md)
tested a revision without this handoff. DSP quality failures and European
translation/UI qualification remain open. See [ADR123](decisions/123-original-clock-handoff.md).

## Next implementation task

Compose the disk writer, original capture/render clock producers and processing
owner in one synthetic recording fixture with advancing original logical time.
Establish genuine prior fits and output priming, retain real captured postroll,
exercise bounded pressure/finite-data/clock-loss cases and join every owner
before settlement and retirement. Keep Windows native duplex activation disabled
until actual SDK and desktop acceptance. Portable finite-prefix recovery and
original-to-project timing transactions remain separate recording work.

## Independent Windows observation

MSVC job114434358685/run38127347962 passed62 synthetic suites145.62s,
including original clock handoff0.02s and monitor processing0.09s. Tested merge
`32e4dc2ccbb5677f09e5c686dae8ab40ea2af5b9` binds headdd46d91/basea7f8d898
and tree26a2460c5e00ecfba096f7d8148efb611ad02e1e. The
[63,227-byte/three-member packet](../tests/results/M2/2026-10-11-monitor-clock-windows/summary.json)
retains complete native job/API evidence and17 reviewed blobs. No per-check count
or executable hash was exposed or inferred. All four stacked CI jobs succeeded;
this is not current-MAIN admission, native SDK/duplex, installed preview or
Windows execution of the later combined disk fixture.
