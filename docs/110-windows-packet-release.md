# Release Windows capture packets before processing

PR #52 merged the bounded capture trace and its retained 0/2/2 native results
after protected checks passed. Those results remain unchanged. This checkpoint
implements the next ownership change; it does not yet qualify a new installed
or native endpoint recording workflow.

## Ownership and bounded preparation

After obtaining actual SDK buffer capacity and before native Start or scheduling
activation, capture prepares one reusable interleaved float packet copy. Capacity
is explicitly bounded by the existing channel/frame admission. Product recording
passes its recording resource ledger: storage and the copy owner are reserved
before allocation. Failed admission creates no writer or running stream. Credit
and backing storage remain owned until stream join and control-side retirement.
There is no callback allocation, storage resize, queue wait, logging or I/O.

The common SDK-independent delivery code performs these steps:

1. Validate extent and copy non-silent data into prepared storage.
2. Return the acquired packet through the native release callback exactly once.
3. Check the actual release HRESULT before invoking processing.
4. Process only owned backing; the consumer must not retain its pointer.

Declared silence never dereferences SDK backing, even if stale. Invalid non-silent
backing is forwarded as null after successful release so the existing input adapter
retains its strict first-error/fault behavior. Oversized/zero extents are released
and refused without processing. A failed release prevents processing and remains
a native stream failure. Successful nonzero HRESULTs remain successful. Packet
frames, flags, device positions and SDK timestamps are not repaired or normalized.
Discontinuity refusal and raw take preservation remain strict.

## Trace version and evidence

New fixture traces use `sc-wasapi-capture-lease-trace-v2`, explicitly marking
processing after release. QPC order is acquire → release → callback return;
held-lease time excludes processing, while callback duration starts at release.
The latter includes dispatch overhead and elapsed descheduling, not measured CPU
cost. Raw SDK timestamps remain in their separate domain. The analyzer accepts
the recorded v1 order independently and recomputes all old evidence unchanged.
Synthetic timestamp controls test both versions and reject reversed/invalid
times without relabeling v1 captures as new native experiments.

The new acceptance test overwrites simulated SDK backing immediately on release.
It then processes a 101-frame, three-channel packet through the real prepared
input/bridge/EQ pipeline, preserving selected channel order and exact raw samples.
The packet exceeds one DSP block, so the check covers partitioned consumption
after release. It also covers silent backing, invalid backing and extent, failed
release, timestamp-error flags, successful nonzero HRESULTs, unavailable consumer,
resource refusal, credit retirement and repeated reuse with no RT allocations
or blocking locks. Storage remains charged throughout reuse.

Current local evidence: focused Linux tests 3/3, AddressSanitizer/UndefinedBehavior-
Sanitizer tests 3/3, actual Windows SDK adapter plus copy/trace targets cross-compiled
successfully, historical media verifier and 17 original refusal controls passed.
These are separate from physical/native endpoint acceptance.

## Native compiler test gate and next task

CI adds a Windows Server 2025 MSVC Release job that compiles the actual SDK adapter
and runs the three SDK-independent packet ownership/admission/trace tests. It uses
the published [runner image's Visual Studio 2022 and Windows SDK](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md).
It needs neither Qt nor libsndfile and does not run endpoint/GUI fixtures. Until
that job executes successfully, native MSVC acceptance of this revision is pending.
Even a passed job is not Windows 11 installer or recording qualification.

Next: build the exact full native fixture in an isolated compiler workspace,
verify limited owner and actual parent/child session dynamically, and run the
same bounded endpoint/source captures with v2 traces on the separate installer
clone. Compare all original failures and new actual exits; retain any gaps and
partial takes. Do not infer that shorter leases solve long event waits. Refresh
the Windows installer only after qualifying its exact source/runtime workflow.

The old refused task cleanup remains recorded in checkpoint 109 for the next
exclusive use of that development clone. No VM was started or changed for this
checkpoint. Existing local preview/source pairs, original VMs and equalizer work
remain preserved. Production-EQ active Stop, monitoring/duplex, physical/sustained
audio and full frozen-reference parity remain open. The full goal stays active.
