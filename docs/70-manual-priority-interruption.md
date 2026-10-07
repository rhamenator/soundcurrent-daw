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

| Native finite case | Stop | Cancel |
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

## Reproduction and next task

Build `sc-manual-interrupt-tests` and `sc-manual-recording-tests`; run CTest
`manual-priority-interruption` and `manual-recording-control`. Build the opt-in
`sc-pipewire-manual-priority-fixture`; use `verify_pipewire_manual_fault.py` with
`--native --mode early-stop` or `early-cancel`, then
`verify_pipewire_manual_priority.py --receipt RECEIPT --output ANALYSIS`.
`manual_priority_verifier_tests.py` accepts the retained receipt. Native tests
follow termination of all owned builders/tests/producers; preserve failures
before diagnosis. User/hardware playback routes are not needed.

Next: bounded serialized Qt manual controller, reliable replies/results, signal
lifetime, native/control/disk shutdown, canonical adoption and late-Cancel policy.
Test actual widgets, repeated takes and rapid monitoring edit/Undo against the
displayed combo state. Full 92 frozen contracts, X004 imports, X005 profiles,
X006 scaling, Europe and native Windows/installer/physical/sustained gates remain
open. No F/Q/C/N promotion, new dependency or persistent schema change.
