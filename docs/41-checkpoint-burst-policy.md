# M2d4c2: admitted recording reserve and checkpoint phases

This extends M2d4c1 after its instrumented native run established a 4.272675-second
write gap and full capture queues. It addresses finite stalls, not sustained
throughput, original historical causes, physical/Windows qualification or parity.

## Bounded admission and ownership

Capture slab size and native callback size remain independent of reserve time.
A capture configuration admits 1..256 pool slots. Both SPSC token queues have
fixed capacity 512; each admitted token owns exactly one preallocated sample slab.
The sentinel is outside every admitted slot. Queue reuse, acquired ownership and
partial-slab finalization retain their existing single-owner contracts. Playback
retains its separate fixed 32-slot pool. No callback resize, allocation, clock,
lock, disk access, logging or GUI operation is introduced.

`withCaptureReserve` admits a minimum 2..20-second reserve off the audio thread,
rounding up whole slabs while preserving the slab/callback sizes. Existing larger
admissions are preserved. Impossible slot counts and per-pipe budgets fail;
requests are never silently reduced. Per-pipe admission includes CapturePipe's
fixed queues/object plus sample storage. Shared recording admission includes each
pool/object, conservative mapper overhead and the playback graph budget before
allocation or creation of any recording jobs. The aggregate ceiling remains
256 MiB. High rates/channel counts may require a smaller reserve; no universal
256-channel recording qualification is asserted.

The desktop offers 2, 5 and 10 seconds, default 10. Prepare captures this runtime
intent along with the model barrier, so later widget changes cannot alter an
accepted preparation. The selector is disabled during preparation/recording/
finalization. Changing a ready preparation applies at the next Prepare. Both
single-track and shared-clock recording pass the normalized reserve to the backend.
The choice is runtime buffering, not project timing, an audio effect or additional
monitoring delay. New labels use translation contexts; translation/UI coverage for
Europe remains unqualified.

## Durability cadence

Writers may specify a first checkpoint threshold in 1..regular interval; zero
chooses the regular interval. Shared recording distributes unspecified first
thresholds by lane ordinal across that interval, before allocating writers. An
explicit first threshold is preserved; the staggering option can be disabled.
Subsequent thresholds retain the regular committed-frame spacing. A checkpoint
is performed after returning the completed slab. Finalization still publishes the
complete accepted suffix and hash with unchanged inactive journal semantics.

The production regular default is 48,000 frames at 48 kHz. Whole-block publication
rounds this up by less than one slab (49,152 frames with 4096-frame slabs). Initial
phases are likewise block-quantized, so some lanes share phases; this is not a
promise of perfectly separated disk calls. Writer scheduling/backlog can also
reconverge phases. The nominal frame cadence is unchanged, but blocked flush or
journal publication can delay durable wall time and leave a longer recoverable
loss horizon. A reserve does not turn uncommitted memory into durable media or
prove power-loss recovery. Written and committed cursors remain separately visible.

## Scoped acceptance experiments

The owned native fixture uses the same admission helper for a 10-second minimum
reserve: 118 x 4096 = 483,328 frames (10.069333 seconds) per capture at 48 kHz.
The workload retains 32 raw mono arms with three non-flat EQ bands, one file track,
signed stereo matrix, independent disk sink, full sample/hash verification, raw
alignment and Save/reopen. No hardware/default route or daemon setting changes.

`stall-absorb` holds lane 17's journal observation for four seconds off audio.
It must complete the entire declared range with zero rejected/missing frames,
exact raw and stereo output, retained origin/alignment and successful Save/reopen.
Its phase facts must show the stall and queued slabs below admitted capacity.
`writer-stall` holds the same lane for twelve seconds, beyond the admitted reserve.
It must retain that initiating capture failure, QueueFull/rejected frames and
independently finalized full raw prefixes. Every raw prefix and common output is
verified without trimming; canonical state remains unchanged. Joined journals
remain inspectable/recoverable under the existing recovery contract.

Ordinary tests qualify pool admission, repeated queue wrap/full-prefix exhaustion,
returned-slot ownership, phase/frame cadence, prepare rejection without jobs,
immutable desktop intent and unfocused wheel protection. Full Debug and sanitized
suites are required because production capture/controller/writer sources changed.
Windows cross compilation only establishes compile/link feasibility.

Long native qualification still requires the unchanged 1800-second sample and
complete current-period timing gates (p99.9 <60%, maximum <80%). Short or injected
runs do not establish it. Every inherited unexplained failure remains in the
checkpoint evidence. Measurements include scheduling and observation overhead;
finite stall absorption does not establish a filesystem/syscall cause.

## Evidence and next task

All five targeted Release and all 27 Debug/27 ASan+UBSan+LSan groups pass.
All existing Windows headless targets compile/link; execution/native/Qt remain
unqualified. Normal20-second and four-second-absorption runs each verify all
30,720,000 raw/1,920,000 stereo samples exactly with Save/reopen and finite callback
timing gates. The absorbed phase lasts 4.003025413 seconds with ready slabs0→46
of118. Twelve-second exhaustion lasts12.003501364 seconds, ready0→118, and names
lane17. All17,988,608 full raw/1,122,304 common stereo samples are exact across
unequal full extents561,152..562,176; canonical state stays unchanged.

Independent-copy recovery invokes the actual developer recovery workflow for all
32 exhausted takes, then reopens each saved state. Every17,988,608 recovered raw
sample matches the original bit-for-bit, sample/media checksums, timing origin,
new/recovered IDs and input-aligned clip extents match. Every original file stays
unchanged; copied source media/journals stay unchanged while copied canonical state
and previous-save backup update as designed. This does not qualify power loss.

A subsequent120-second native run verifies all184,320,000 raw/11,520,000 output
samples, peak4.1435, full Save/reopen and complete callback timing. Owner p99.9
4.805532ms/max13.698197ms are22.526%/64.210% of the observed1024/48000 period;
source/sink also pass. Largest observed queue is1slab, flush89.351638ms,
journal56.715259ms and write gap98.962843ms. Phase pairs complete. These observations
do not establish the historical stall cause or sustained30-minute qualification.
Defaults/prior links stay unchanged and all owned nodes/links retire. Sources are
frozen before launch and every inherited unresolved observation remains retained.

See [checkpoint evidence](../tests/results/M2/2026-10-06-checkpoint-burst-policy.json)
and the goal-progress entry for exact measured scope. All 92
frozen acceptance/quality/reference/F/Q/C/N contracts remain intact and unpromoted.
After bounded policy qualification, run the required 30-minute native workload
and retain full timing/source/media facts. Physical interfaces, Windows native
execution, controlled competing loads, additional filesystem/power-loss/unload
reliability, punch/loop/takes/comping and the full product backlog remain required.
