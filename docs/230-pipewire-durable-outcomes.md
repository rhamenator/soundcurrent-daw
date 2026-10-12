# Durable PipeWire routes and joined recording outcomes

Checkpoint230 adds schema1.0 durable owner state alongside the verified paired raw
and native-receipt journal. Native controller/Qt integration, verified alignment,
installed/physical qualification and full workflow parity remain required.

## Publication and recovery

A separate state lease remains held across raw/receipt finalization and outcome
publication. One serialized control owner writes bounded immutable GUID artifacts,
flushes/publishes them, then atomically replaces a16KiB manifest. Initial intent,
selected stable port intent and terminal outcome references commit independently.
An interruption leaves the previous complete manifest; orphan artifacts remain
untouched. An inactive inspector holds the state lease while verifying artifact
names/extents/hashes and the exact paired PCM/stamp/receipt checkpoint.

Terminal state binds all checkpoint counts and prefix hashes, run/project/track/
asset identities, generation, expected graph shape and POSIX clock domain. Stored
Recorded claims require healthy joined native facts, startup provenance, matching
extents and finite native return evidence. A failed run may retain larger observed
native extents than its older durable prefix; those extents are not promoted to
committed media. Missing terminal publication remains explicitly pending.

Linux flushes file and directory publication; Windows retains the existing file
flush/write-through policy without a directory/power-loss guarantee. Hashes prove
stored consistency, not authentication or physical hardware attestation.

## Stored original state

Intent retains expected geometry and all public processing queue/capacity/wait/
warmup/first-ordinal declarations. It stores stable ordered capture/output ports,
initial EQ and explicit channel-map artifacts through existing canonical codecs.
Resource facades are not loaded from files. Decoding validates declarations without
allocating audio banks or activating SDK/device owners.

Joined outcomes retain native capture/render first/last/content/fault clocks and
cycles, actual native queue results, counters, join/stop/completion flags, startup
statistics and exact EQ/map fingerprints, processing horizon requests, fanout/
clock/buffer/EQ/receipt faults, worker wait counts and typed failure-presence flags.
Exception text is intentionally not persisted as executable or trusted state.
Rejected cycle facts retain all256 fixed port cells and original invalid values;
nonfinite diagnostic peak values retain their exact binary64 bits. Successful
claims receive separate wire-structure and semantic validation.

JSON has strict known fields/enums, duplicate-key/depth/event/byte limits. Artifacts
are at most4MiB, read through64KiB chunks with cancellation/EOF checks and trusted
24MiB workspace admission. Native state/paired/raw leases refuse active mutation.
No timestamp fitting, project attachment, native reactivation or automatic speaker
correction occurs during inspection. Initial EQ state is not a replay log of live
parameter edits; those edits stay in the existing applied-event system.

## Qualification

Implementation **24b0465** binds829 source/checkout inputs. Three current Release
suites pass2.44s; state tests cover four actual process interruptions. Six Windows
state/codec/owner/fixture objects compile warning-free, without native runtime
qualification. A genuine private PipeWire1.6.2 run passes99,072 raw frames and
8,192 monitor/sink frames with387 capture/408 render receipts, zero audited
callback allocate/free/lock hits, and exact reopened intent/startup/native facts.
Three current ASan/UBSan suites pass4.93s with leak detection disabled; all four actual
publication interruptions also pass under sanitizers. The sanitizer private
PipeWire route passes100,352 raw frames and8,192 monitor/sink frames with392
capture/413 render receipts and exact reopened durable state; audited callback
hits remain0. The corrected owner and deterministic render suites pass ThreadSanitizer
in4.33s without suppressions. Original failed attempts remain retained.

Tests reopen actual joined owner results for success, empty cancellation, partial
activation failure, callback rate mismatch and disk failure. Standalone state tests
kill actual child processes at artifact flush/publication and before/after manifest
publication, and verify pending versus terminal state correctly. Unicode/moved
projects, active leases, altered hashes, contradictory rehashed Recorded outcomes,
unknown versions/enums, duplicate keys, quota/cancellation and rejected original
widths/nonfinite bits are covered. Original PCM remains untouched.

The first artifact reader incorrectly requested the shared128KiB readBounded API
for a larger state file; it correctly refused. The reader now uses64KiB exact chunks
and an EOF probe without relaxing the shared I/O limits. Original failures/source
are retained. Initial indentation warnings and a Windows-only fixture warning are
retained/corrected by separating statements; no failure gate is suppressed.

## Original local sanitizer failures

The first TSan owner attempt at58f0ffd failed in2.60s with capture/render
ClockRejected at original cycle209. It had98,880 preserved raw frames,480
queued monitor frames and no processing/drift fault. The old result omitted
clock-handoff cause/counts, so the original rejection root remains unknown; no
race report was emitted. The actual failed project and source/logs are retained
locally; public evidence contains metadata and hashes, never audio bytes.

A diagnostic-only revision then failed terminal publication in2.51s after all
2,400 monitor frames reached output. The validator incorrectly required the last
audio-bearing receipt itself to declare producer completion. Producer EOF may
arrive after that receipt, yielding a real final silent returned buffer. Validation
now requires the actual last joined receipt to be final/allQueued while preserving
the distinct lastContent receipt unchanged. A deterministic render regression
checks this ordering, silence, exact counters and no premature completion.
This fixes that predicate; the later TSan pass is not a causal explanation of the
original clock failure. Additional callbacks after a silent completion can place
a later non-final silent receipt in last; the current conservative validator may
refuse that ordering. Retaining an explicit completion receipt is a follow-up,
not a qualified workflow claim. New after-join clock fault/count observations improve future
owner diagnostics; those additional handoff facts are not yet in the state schema.

## Original PR164 failures

Original run38317455660: Linux152/431.76s, Windows desktop20/97.65s and cross pass.
Native selection passes78/79 checks in156.29s; new owner workflow fails after0.53s
with processing Clock cause0 and capture RawOrMonitorRejected cause10. The old extra
concurrent diagnostic completes all three20-take attempts; that does not fix the
previous PR163 pool-pressure cause. The current owner rejection root remains unknown.
New typed fanout fault/pre-cancel count observations distinguish raw versus monitor
refusal on subsequent revisions without changing slots, cadence or deadlines.

The Windows retention self-check additionally fails rewriting its log after a
foreign-path refusal. The lazy ReadLines enumerator retained a Windows handle when
foreach threw. It now uses a bounded eager read that closes before policy checks.
Self-test and actual retention run in separate always steps; a self-test failure
cannot skip real retention. New owner-test failure markers use explicit UTF-8 and
bounded generated native-temp paths so future failures retain the actual project.
Local helper tests cover exact native/manual bytes, Unicode, path and reparse
refusal; independent Windows execution remains pending. The failed native owner project was not present in the original258-member
archive; its actual bytes are unavailable. Original logs/selected artifacts are
preserved; no unchanged rerun replaces the failures.

## Next task

Integrate durable state and the owned route with the native recording controller
and Qt workflow. Reopen saved raw takes through verified admission/explicit user
placement and export; do not infer recording alignment or audible drain from queued
frames. Continue causal investigation of the Windows owner rejection and previous
pool pressure using retained original observations. Full installed/physical/quality,
Windows workflow parity, later milestones and reviewed European localization remain
required.

Evidence: [bound qualification summary](../tests/results/M2/2026-10-12-pipewire-durable-outcomes/summary.json).
