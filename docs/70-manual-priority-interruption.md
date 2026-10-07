# Manual recording: priority Stop and Cancel

Scoped checkpoint, 2026-10-07 UTC. Full goal incomplete. See the
[dated receipt](../tests/results/M2/2026-10-07-manual-priority-interruption.json).

## Ownership and callback boundary

`ManualRecordingInterrupt` retains a one-generation, always-lock-free 32-bit
atomic signal. Any thread may request Stop/Cancel. Cancel implies Stop and
dominates earlier Stop; there is no reset. Prepare shared ownership off RT and
retain it through native/control joins. No mutable endpoint, route, disk, pool or
GUI object escapes through the signal.

The next callback observing it requests terminal Stop on the existing bridge,
silences output and closes the raw prefix independently of disk startup. The
request does not fabricate callback acknowledgement: status may remain Ready or
Running until observed by audio. Serialized preparation/submission refuse new
work immediately after observing the signal. Native activation also refuses an
already signaled generation; that guard has source/build coverage, but a separate
native pre-activation workflow is not yet qualified.

Nonblocking disk service checks between lane construction/join operations and
stops starting further work. A constructor/verifier already executing may still
take time. Finalization joins native callbacks first, then the reader and every
disk consumer, including consumers not serviced before interruption. Stop keeps
explicit partial/recovery choices. Cancel sampled at finalization entry marks
the group canceled, refuses adoption and preserves recoverable originals.

**Cancel during/after finalization remains unqualified.** Delivered groups are
not retroactively rewritten. The upcoming Qt controller needs an explicit
finalization/adoption boundary and reliable late cancellation policy.

## Acceptance evidence

Synthetic Stop and Stop-to-Cancel before the first callback retain queued In/Out
terminal replies without starting a take. Stop/Cancel while a real lane-zero
constructor is held verify 32 arms, latencies 0/41/200/4097, exact raw lengths and
origins, every durable sample, silence, immediate admission refusal, no additional
startup work, pool retirement and saved-project preservation. Stop previews
explicit partial adoption; canceled groups refuse adoption. RT allocations,
frees and blocking locks are zero. Debug, Release and ASan/UBSan/LSan pass both
new interruption and existing manual-control tests (2/2 each), with leak detection
explicitly enabled. Windows media targets cross-compile; runtime is unqualified.

The native diagnostic uses owned synthetic PipeWire nodes. Its simulated GUI
producer reads only atomic status/position and requests the signal. Lane-zero
construction stays held until actual callback application/completion. The
observer copies the entire produced prefix and joins before recorder deactivation
and disk draining. Existing full raw/recovery/nonflat-output, receipt, adoption,
timing, cycle and owned-route gates remain required.

| Initial native finite case, before review | Stop | Cancel |
|---|---:|---:|
| Requested / stopped engine frame | 49289 / 49289 | 49289 / 49289 |
| Advancing / terminal owner callbacks | 96 / 1 | 96 / 1 |
| Raw and recovered samples verified, each | 25,408 | 25,408 |
| Entire mixed output samples verified | 98,304 | 98,304 |
| Maximum owner wall / CPU time (ns) | 1,878,464 / 1,872,875 | 1,314,686 / 1,310,311 |
| Actual quantum / rate | 512 / 48 kHz | 512 / 48 kHz |
| RT allocation / free / locks / late cycles | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |

All 96 advancing callbacks have complete 32-channel source/received markers. The
terminal nonadvancing callback is independently counted by stage coverage; its
waveform is outside the advancing-row marker oracle. Fifteen altered receipts
are refused per retained native result, including missing/false held/applied
terms, wrong Cancel, ordering/quantum violations and original raw/RT/cycle gate
failures. Mutation checks do not rerun native audio. Defaults/prior links remain
unchanged and owned routes clean up.

## Preserved originals 42/43

Original 42: initial Cancel unit workflow reported `Priority raw header failed`.
Failed lane/open/header terms were unlogged and remain unknown. Independent
original-file inventory shows lane-zero's capturing journal with 243 committed
frames and only `audio.partial.rf64`; other positive lanes have finalized
`take.wav`. The test hardcoded `take.wav` for canceled durable media. It now opens
the checkpoint's source path and logs future header-failure terms. Production
cancellation was not changed for that filename assumption.

Original 43: first native priority Stop applied interruption while startup was held,
retained 97 owner callbacks and passed timing/marker gates, but its observer failed
after accepting 50176 frames for 49152 produced frames. Failed clock 5538201088
follows 5538200576 contiguously (512 frames/id35/cycle8117876). Recorder deactivation
preceded 32 disk joins while the observer remained live. Original failed pointer/
size/backing terms were unlogged. A missing output buffer is a lifecycle/source
inference, not a recorded pointer observation. The priority fixture now waits for
the entire produced prefix and joins its observer before recorder deactivation;
no prefix, continuity or timing gate was relaxed. The ordinary fixture is unchanged.

Both originals were frozen before diagnosis/rebuild. Later passes do not resolve
original 36's old early Stop, original 37's channel 19 extra 512-frame prefix,
original 38's 13.88ms predominantly-CPU overrun or earlier sustained failures.
The archive retains full generated original media/journals/traces, logs, receipts,
exact source versions and frozen executable hashes. No executable/user audio is
included. It is evidence, not one loadable project. All 43 historical observations
remain linked through prior receipts.

## Review regressions and original 46

Review found missing interruption checks between control-thread lane verification
operations. Original 44, a deliberate GNU/Linux linker regression against the
initial core, records 32 actual inspections after Stop during the first. Checks now
defer remaining verification/retirement to blocking finalization. The same test
records only 1 inspection, then verifies all 32 complete 87-frame lanes after join.
No production inspection hook or raw/hash requirement changed. Final Debug,
Release and sanitizer tests pass 2/2; Windows media compiles. The verification
interposition regression itself is GNU/Linux only.

Review also found the producer could signal before startup ever entered its gate.
Original 45 deliberately delays control service until In+1536, with the old
position-only producer target In+1000: request/stopped 49289, disk_held=0 and
held_at_request=0. The producer now waits for the held flag and position, records
held-at-request, and still requires callback completion before releasing startup.
Joined timing/traces were not emitted before the original assertion; unavailable
original terms remain unavailable.

The first corrected late-service Stop attempt, original 46, meets priority/timing
and observer-prefix gates but fails the raw waveform oracle. Actual quantum was
256 frames at 48 kHz. All 1,651 saved lane 17/source 23 samples match an extra-256-frame
source delay; the other 23 positive lanes match the unshifted source. All 194
advancing owner rows show the same source 23 marker delay before capture. The 196
owner callbacks include 2 nonadvancing rows. Maximum owner wall/CPU 803257/800547 ns;
no late-cycle/RT violation. Root cause remains unresolved. Nothing is compensated.
Final-source native Stop is unqualified and was not retried merely for a pass.

The separate final Cancel workflow passes at256/48kHz: request/stopped 49801,
195 callbacks (194 advancing/1 terminal), complete 32-channel correspondence,
37,696 raw/recovered samples and 99,328 mixed-output samples verified. Maximum owner
wall/CPU 747136/743476 ns, zero RT/cycle violations. Eighteen altered receipts are
refused, including held-at-request and delayed-service evidence. This pass does
not explain original 46 or previous channel delays. Initial 512-frame passes above
remain historical evidence tied to their exact earlier source/executable.

The review archive retains originals 44–46, their full generated files and missing
terms, independent original 46 analyses and the final Cancel case. All 46 historical
observations remain linked. The original evidence archive is unchanged.

## Reproduction and next task

Build `sc-manual-interrupt-tests` and `sc-manual-recording-tests`; run CTest
`manual-priority-interruption` and `manual-recording-control`. Build the opt-in
`sc-pipewire-manual-priority-fixture`; use `verify_pipewire_manual_fault.py` with
`--mode early-stop` or `early-cancel` (native is the default), supplying both
`--output RECEIPT --failure-output FAILURE`, then
`verify_pipewire_manual_priority.py --receipt RECEIPT --output ANALYSIS`.
`manual_priority_verifier_tests.py` accepts the retained receipt. Native tests
follow termination of all owned builders/tests/producers; preserve failures
before diagnosis. User/hardware playback routes are not needed.

Next: diagnose original 46's native source/received channel delay without compensation,
using its original route/buffer/clock evidence. Then bounded serialized Qt manual
controller, reliable replies/results, signal
lifetime, native/control/disk shutdown, canonical adoption and late-Cancel policy.
Test actual widgets, repeated takes and rapid monitoring edit/Undo against the
displayed combo state. Full 92 frozen contracts, X004 imports, X005 profiles,
X006 scaling, Europe and native Windows/installer/physical/sustained gates remain
open. No F/Q/C/N promotion, new dependency or persistent schema change.
