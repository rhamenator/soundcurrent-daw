# Original capture handoff to raw and monitor owners

`PreparedCaptureFanout` is an original GPL-3.0-only C++20 transport component.
One capture producer supplies two independent prepared banks: a raw recording
consumer and a monitor processing consumer. This checkpoint advances P005/M2;
it does not enable the native Windows duplex factory or change user audio.

## Ownership and admission

The existing stamped bank validates immutable identity, native frame/QPC units,
flags, original packet continuity and sequence bounds. Both routes share that
format and initial counter configuration, with separate slot counts, sample
storage, queues and consumer leases. Each consumer owns one outstanding view.
Cross-route, stale and modified releases are refused. SDK backing must remain
valid until `publish` returns; consumers never retain that backing.

The entire group is charged before either bank allocates or touches storage:
outer owner state plus both complete bank payload charges. A single outer
resource lease retires after the banks; destroy only after all three owners join.
Configuration remains bounded to 1–256 channels, 8–384 kHz, 1–65,536 frames per
packet and 1–256 slots per route, subject to admitted memory credit. These are
current component bounds, not a product track limit. Slot capacity is not a
guaranteed disk-stall time reserve when native packets have variable sizes.

## Publication and faults

Complete raw copy and publication precedes complete monitor copy/publication.
Neither operation waits for disk durability or DSP. This is two independent
queue publications, not an atomic transaction. A raw refusal accepts neither
route. A monitor refusal after raw acceptance preserves that additional whole
raw packet; separate accepted-frame fields and the first fault expose precisely
which route accepted it. Future packets are refused on both routes after a fault.
Native producers cannot retry a packet as though this partial delivery were absent.

Original float32 bits, channel order, headroom and original stamps survive copying,
including signed zero and exceptional float bit patterns. Silent packets produce
zeros without reading their SDK backing and retain original silent metadata.
This does not assert preservation of integer-encoded native PCM bytes. Conversion,
nonfinite DSP policy and durable raw/project mapping remain separate work.

Stop/cancel requests are read once before publication; an in-flight packet may
finish both routes. Producer finish ends both banks before publishing outer done.
All accepted prefixes remain drainable after Stop, cancel or fault. No end timestamp
is invented. Consumer failure is immediately visible in status; the producer
records it on the next attempt or finish, without inventing an attempted packet
when no attempt occurred. After all three owners join, `settleAfterJoin` transfers
producer ownership to control and records late consumer failure idempotently.
It does not discard queued prefixes. The first observed fault remains immutable;
this does not assert wall-time ordering among simultaneous independent failures.

Prepared operations use bounded copies/arithmetic/queues and atomics, without
allocations, blocking locks, disk I/O, logging or SDK calls. Two copies increase
memory bandwidth and storage demand; profile actual supported configurations
before considering a shared three-owner pool. See [ADR119](decisions/119-original-capture-fanout.md).

## Frozen qualification

Source `ead72b376d25a3cf0e0b13864e22ae7acba388d1` passed the fanout and existing
bank suites in Linux Release (0.08s), ASan/UBSan with leak/UB checks (0.73s),
and TSan with halt-on-error (0.76s). Each profile made 97,086 fanout checks and
3,179 existing bank checks. Actual concurrent producer/raw/monitor fixtures cover:

- Three channels, 20,000 variable packets, 319,935 frames per route and 1,919,610
  float-bit comparisons across both routes.
- 256 channels, 1,024 variable packets, 16,369 frames per route and 8,380,928
  float-bit comparisons across both routes.
- Queue sequence wrap, original integer origins above 2^53/2^63, reused/poisoned
  source backing, silent packets, distinct consumer scheduling and leases.
- Whole-group admission/retirement, raw-full and monitor-full partial delivery,
  future refusal, consumer loss with/without a new attempt, late post-done loss,
  joined settlement, original fault retention and exact Stop/cancel prefixes.

The synthetic source waits for conservative consumer credit outside audited
methods; native callbacks cannot wait this way. Explicit overflow tests qualify
the refusal path separately. Guarded C++ allocation/free and Linux-interposed
libc allocation/free/pthread-lock counts were zero. This is not a scheduling
deadline, disk throughput, native endpoint or complete syscall audit.

The [55,352-byte evidence packet](../tests/results/M2/2026-10-11-capture-fanout/summary.json)
retains 12 members, 556 selected source inputs, six executable hashes and full
build/CTest/LastTest logs. Fresh Release bank/fanout/test compilation produced the
exact previously executed binary hashes. Worktree and Git blob hashes are separate;
the Windows desktop preparation script has an explicitly verified CRLF checkout
of its LF Git blob. Standalone gzip/member and frozen source checks passed.
No executable or test media is included.

## Independent Windows execution

Actual MSVC job114416549160/run38121349153 passed58 synthetic suites151.32s,
including fanout0.08s. Its original tested merge
`02e5ac9491a77702ff3a314170d8b1eddb6fc2df` binds headba0a2048/basedac5a591
and exact treea9e07a98. A [42,140-byte packet](../tests/results/M2/2026-10-11-capture-fanout-windows/summary.json)
retains full native job/API state and19 reviewed source hashes, checked against
the exact Git blobs. Standalone member integrity passed. Per-component check
counts and executable hashes are not exposed by this log and are not inferred.
This is independent synthetic Windows execution; a stacked successful cohort
does not qualify current-MAIN admission, native SDK, installed or physical audio.

## Next implementation

Build the charged monitor output slab/SPSC bank with generation-bound opaque
output ordinals, exact partial pulls, explicit underflow/loss, headroom, Stop/cancel
and joined retirement. Do not present these ordinals as native capture timestamps.
Then compose the raw disk writer with versioned original stamped metadata and
raw/project correspondence, the existing causal DSP/controller, and actual SDK
owners. Implement clock observation before monitor activation, matching admission,
control-point freshness and complete failure/shutdown retirement.

Actual disk/DSP/output/native composition, installed Windows duplex, quality/load,
earlier DSP quality failures, M2–M11, X004/X005 and full F/Q/C/N remain open.
European translation, native review and UI qualification are still incomplete.
