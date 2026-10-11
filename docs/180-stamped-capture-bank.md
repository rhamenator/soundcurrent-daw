# Stamped capture-bank ownership core

The Windows duplex path now has a prepared packet bank for handoff between a
capture owner and a processing/render owner. This is a tested core prerequisite;
it does not enable native Windows simultaneous recording/playback yet.

## Workflow and contract

Prepare immutable stream identity, sample rate, original channel count/order,
packet capacity and slot count off RT. The bank charges its object/queues/sample
pool through the existing resource ledger. Default capacity is 32 packets of 1,024
frames; current admission permits 1..256 channels/slots and1..65,536 frames, subject
to the configured/shared memory budget. Bounded capacity is not a callback deadline
guarantee. Allocation/touching, reserve changes and final ledger retirement occur
outside RT; storage is retired after both owners join.

Publish copies a complete packet and its ORIGINAL frame position, 100ns QPC,
identity, extent and flags. No SDK pointer or reusable callback scratch escapes.
The consumer receives one explicit lease and returns exactly that token once.
Channel order and float bits/headroom, including nonfinite input, are unchanged;
downstream DSP/raw-writer policy remains explicit. Flagged silent packets get zero
samples without reading native backing and retain their original silent flag.

The existing lock-free SPSC queue transfers free slot IDs and ready tokens.
Queue counters may wrap; packet sequence overflow cannot reuse stale identities.
Altered/stale/foreign release and duplicate acquire are refused. Queue overrun,
invalid packet shape/backing/flags/timestamp, identity change, later discontinuity,
position gap/overlap, QPC reset or arithmetic overflow is terminal, preserving
the original first rejected stamp and lost-frame count. Already accepted prefixes
remain drainable. Returning a slot cannot disguise an overrun as a recovered run.

Empty is explicit temporary underflow; it creates neither audio nor a timestamp.
The later causal monitor owner must handle warmup/underflow using its declared
delay policy. Stop/cancel requests do not block; an in-flight publication may
complete. Capture-owner finish release-publishes Done after its final prefix;
consumer checks the queue again once if Empty races with Done. Cancellation before
finish dominates Stop. SDK acquisition/release remains on its original owner.
No native origin, project frame mapping, end timestamp or corrected raw take is
synthesized. [ADR116](decisions/116-stamped-capture-bank.md) records the boundary.

## Frozen local acceptance

Source `2da7a3e2f664963f9230161031fa04290969527c` passed three Linux Release
suites in 0.07s: stamped bank, clock correlation and WASAPI packet copy. The bank
passed ASan/UBSan with leak detection/halt-on-UB in 0.13s and a separately configured
ThreadSanitizer build with halt-on-error in 0.26s. All configurations compiled/linked
the frozen bank/test bytes. Local acceptance has 3,179 bank checks, including:

- Original float bits, NaN/Inf/signed-zero/headroom, three-channel order and an
  admitted 256-channel/96kHz packet, without downmix or buffer truncation.
- Native SDK-release-before-callback simulation, immediate SDK reuse and later
  intermediate-scratch reuse before bank consumption.
- Queue counter wrap, exact lease return, stale/altered/foreign lease refusal,
  sequence overflow, strict overrun and original fault-prefix retention.
- Timestamp/position/flag/shape failures, explicit empty, Stop/cancel ordering,
  consumer failure, idempotent finish and held-lease retirement protection.
- Failed memory/shape admission without leaked credit; processing retains all
  admitted credit until joined-owner destruction returns it off RT.
- 20,000 packets/319,935 frames on two actual threads with independent fixture
  pacing outside audited operations. Every sample, packet sequence, original
  position/QPC/extent and clock-core observation is checked. Scratch is overwritten
  after each publish; both owners drain the complete accepted prefix.

Audited owner paths have zero C++ allocation/free and zero interposed libc
allocation/free/pthread blocking-lock calls. This is test-only interception, not
every library/syscall path or a physical callback deadline proof. Windows builds
report their smaller interception scope explicitly and compile-check flag values
against SDK constants; actual Windows bank execution remains a separate gate.

The [31,729-byte packet](../tests/results/M2/2026-10-11-stamped-capture-bank/summary.json)
retains 11 logs/hash members,538 selected source/test/UI/catalog/CI inputs and five
executable hashes across Release/ASan/TSan profiles. Inputs are not an exhaustive
compiler/system/third-party closure. Standalone validation and optional matching
source-byte comparison passed; there is no replay/signature claim. No executable,
installer, SDK or audio media is included. Reproduce with target
`sc-capture-bank-tests` and CTest filter `^stamped-capture-bank$` in each configured
profile. The explicit Windows MSVC CI target/allowlist now includes this suite.

## Remaining work

This original GPL core reuses existing repository infrastructure and adds no
dependency, driver, signing certificate, raw-media/project schema or audible UI
change. [Clock-core Windows execution](179-native-clock-correlation.md) has separate
synthetic evidence. Neither primitive qualifies native Windows duplex, causal
monitor delay, drift-resampling quality, durable mappings, recovery, installation,
physical/load workflows, localization or full P005/M2/F/Q/C/N. Earlier processing
quality failures remain open.

Next: prepared causal monitor buffering and bounded drift correction using each
stream's original clock observations. Preserve original raw samples and durable
versioned raw-to-project mappings separately. Then compose native capture/render
owners, raw writers, playback/EQ and finite/punch/loop/Stop postroll before enabling
the Windows desktop duplex factory and qualifying owned native endpoints.
## Independent Windows MSVC observation

Actual Windows native-unit job114396486497 in run38114463559 passed54
synthetic suites in197.10s, including stamped-capture-bank in0.03s. Tested merge
`ab12250419fb1329e33964a48d3a7bd535cd44a0` binds head
`d55b6661f19db96a4ae10f9b49006d1af672e166` and stacked base
`9c8f8cfa9f29631850bd80df1085f76977024903`; the API-verified merge tree matched
the reviewed head. This is actual Windows/MSVC execution with simulated original
packets, not native endpoint or installed duplex qualification.

The [38,461-byte/three-member packet](../tests/results/M2/2026-10-11-stamped-capture-bank-windows/summary.json)
retains the complete job log/status and15 changed-file hashes. Archive/member
validation passed. Per-bank check counts and executable hashes were not exposed;
neither is invented. The successful stacked cohort is not current-MAIN admission.
Subsequent manual-owner diagnostics and fresh protected checks remain required.
