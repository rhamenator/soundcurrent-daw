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

Review additionally identified a compound-admission leak: input conversion was
assigned to recording state before native packet-copy admission succeeded. Both
owners are now staged locally and committed together after preparation. A native
`admission` fixture prepares an explicit inactive loopback route, budgets exactly
enough for input conversion, requires three packet-copy budget refusals with
unchanged active credit, then retries with sufficient credit and verifies full
retirement. It never activates recording or creates a writer. Execution of this
regression fixture is pending the isolated native build; it is not counted as a
passed native workflow merely because the source exists.

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
the installed MSVC discovered through `vswhere`, with its developer environment
and Ninja. The first hosted attempt refused configuration before compilation:
the `windows-2025` label supplied image `windows-2025-vs2026` version
`20260925.250.1`, so the hardcoded VS 2022 generator found no instance. That
refusal remains in hosted run `37817576970`; it is not a failed packet test.
The corrected job discovers the installed compiler instead of assuming its year.
The corrected hosted job passed all three packet tests on MSVC 19.51.36260.0
at `cfa5b5b` (run `37818166925`, job `113452281276`), then again at the
admission fix `ac11b80` (run `37821506179`, job `113463413902`). That latter
job did not build the media-dependent `WasapiRecording` or endpoint fixture.
The native check is now one of the three required strict branch-protection checks,
alongside Linux desktop/synthetic tests and Windows core cross-compilation.
These passes do not qualify Windows 11 installation or endpoint recording.

The check is extended to compile the production recording owner and full native
capture/admission fixture using the same pinned libsndfile 1.2.2 source and
codec-disabled shared build as the Windows preview. Its archive is verified
against SHA-256 `ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121`
before extraction; no new dependency is selected. The hosted job also runs the
existing recording/recovery, offline WAV export and resource-ledger tests,
without activating audio endpoints. The expanded gate passed six selected tests at `58a23cb` on hosted
MSVC 19.51.36260.0, run `37846953989`, job `113550078108`. Its retained
[receipt and native CTest log](../tests/results/X007/2026-10-08-hosted-native-recording/receipt.json)
record 255 recording/recovery checks on 480,000 synthetic raw frames, 1,062
export checks across 1/2/8/32/256 channels and 44 resource-ledger checks, plus
three packet/admission/trace tests. This ran six of 37 configured tests. The
endpoint fixture and production recording owner compiled but were not activated.
Recording/recovery took 57.17 seconds; its Windows CTest observation budget is
extended from 60 to 180 seconds, with test cases and internal failure assertions
unchanged. Linux retains 60 seconds. The timeout adjustment is pending its check.
The local MinGW full fixture, including production admission code, compiles.
This is compilation evidence, not execution of the inactive native route test.

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

## Reboot and resource contention

A host reboot interrupted independent native test-workspace preparation. The new
full-copy VM is defined and its qcow2 has no backing file, but equality with the
powered-off source disk is still unverified. Independent TPM and firmware copies
are byte-verified. The test clone is prepared with 8 GiB RAM, four vCPUs capped
at two CPU equivalents, 8 MiB/s guest read/write limits, an isolated network and
no installer CDs; it has not been booted. The owned read-only equality verifier
was suspended in response to owner-reported disk contention, then the same
process resumed after host load fell. A systemd-managed scope enforces 16 MiB/s
read and one-quarter CPU limits with idle I/O priority. A sampled disk utilization
of 3–4% confirmed the cap, separately from throttling-induced pressure counters.
Originals and ongoing equalizer work remain untouched. No second DAW VM is started. Full native admission/endpoint tests and the source
PR's review resolution remain pending. Existing preview artifacts are unchanged.
