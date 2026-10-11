# ADR117: causal monitor owner and explicit drift-control grid

Status: selected implementation direction after bounded kernel feasibility;
production buffering, drift control and native integration remain to implement.
Date:2026-10-11. Extends ADR114–116 without changing the frozen product baseline.

## Existing dependency and alternative evaluation

Reuse the unchanged, hash-pinned libsamplerate0.2.2 BSD-2-Clause best-sinc kernel
selected in ADR096. Exact upstream revision, complete files and notices remain in
`third_party/manifest.json`; no source fork, new binary dependency or system install.
Linux C/C99 and Windows MSVC/MinGW build support is already independently exercised
for fixed conversion. Variable monitoring still needs its own native qualification.
The mature pin's maintenance/security review remains separate from this experiment.
Retain all unused helper notices and the GPL later-version option described in
ADR096. The new harness and future owner are original GPL-3.0-only code.

| Choice | Functionality / integration cost | Decision |
|---|---|---|
| Call existing PreparedResampler from native callbacks | Fixed ratio, worker validation/exceptions, equal-rate bypass, exact finite fixed-rate end; no adaptive callback contract | Refused for this role; preserve existing API |
| Restart a converter on each packet | Loses filter history/phase and causal continuity | Refused |
| Use arbitrary callback capacities as implicit ratio ramps | Different call partitioning yields different rate trajectories | Refused as the control policy |
| Persistent best-sinc state with explicit ratio on a fixed output-frame grid | Known pin/notices, extra filter storage and lookahead; needs charged buffers and bounded controller | Selected for prepared processing owner |
| New soxr dependency or linear interpolation fallback | Additional integration/license/quality work or inadequate established stop-band evidence | Not selected; ADR096 evaluation remains |

The [full API](https://libsndfile.github.io/libsamplerate/api_full.html) defines ratio
as output/input, consumed/generated frame counts and persistent streaming state.
`src_set_ratio` explicitly sets the next starting ratio; changing only `src_ratio`
requests automatic transition. The [official FAQ](https://libsndfile.github.io/libsamplerate/faq.html)
explains filter transport delay and retained state across chunks. Preserve floating
headroom. Do not reset, clone or allocate converter state during live processing.

The pinned mono/stereo/multichannel variable kernels interpolate against the
requested output capacity and save the actually reached ratio. This source
inspection explains the probe observation; it is not a vendor defect claim.

## Prepared processing contract to implement

1. Reserve owner/state, input and output slabs, token queues and converter allowance
   before allocating/touching memory. Use existing ResourceLedger credit and the
   pinned512KiB/channel allowance. Always prepare a converter, including nominally
   equal rates: separate endpoints may drift. Configuration/retirement stay off RT.
2. A single processing owner consumes original stamped capture packets. Raw audio
   and original clock metadata go to a separate bounded disk-writer route before
   any monitor conversion. Disk work, GUI, logging and blocking waits remain outside
   native callbacks. Conversion does not rewrite raw packet stamps or fabricate a
   shared DeviceBlockClock/CaptureTimingOrigin.
3. Use64 emitted output frames as the initial control grid, independent of input
   packets/render callback partitions. Hold one explicit ratio throughout a grid
   interval, including calls that generate only part of it. Preserve converter
   history/phase across rate changes. The later controller must bound ratio and
   per-grid slew; the probe's stepped schedule is not a quality-qualified controller.
4. Derive drift estimates from validated original capture/render fits, identity,
   generation, frequency, span, age and declared uncertainty. Subtract large integer
   origins before conversion. Add bounded buffer feedback only with an explicit
   tested control policy. Never silently clamp bad/reset/stale clocks into plausible
   timestamps or substitute occupancy for durable recording alignment.
5. The monitor buffer admits an explicit delay sufficient for actual causal source
   availability and algorithm context. Warmup/temporary Empty is distinguishable
   from accepted silence. Publish prepared output slabs to the render owner's SPSC
   route. On post-warmup underflow, overrun, nonfinite DSP output or clock loss, fail
   explicitly and preserve raw prefix/recovery context; never replay stale audio or
   extend the live source with invented future zeros. A native owner must obey its
   terminal output-silence policy while processing joins.
6. Bound input copying, converter calls and emitted frames per service invocation;
   report NeedInput/NeedOutput without unbounded retries. Original source frame
   counts and output/control-grid counters are separate domains with overflow checks.
   Stop, cancellation and tail/drain policy require explicit acceptance, especially
   for finite/punch/loop postroll. Converter lookahead is not physical device latency.
7. Quiesce/join capture, processing, render and disk owners before reclaiming their
   buffers/state/credit. Partial activation/fault preserves original recovery jobs.
   Versioned raw-to-project correspondence remains separate from monitor PCM.

## Evidence and remaining gates

[Checkpoint182](../182-monitor-kernel-feasibility.md) records packet partition,
causal startup, full channel-width and audit observations. Its finite supplied
signal is read with `end_of_input=0`; no end padding, native endpoints or raw/project
schema is used. It establishes a narrow kernel feasibility condition, not latency,
control stability, scheduling, spectral/phase/transient parity or a usable monitor.

The upstream sanitizer switch always enables ASan/UBSan. Our build adaptation now
disables that switch and forwards the parent's actual `-fsanitize` profile to the
C target, avoiding incompatible ASan/TSan combinations while leaving upstream files
unchanged. Actual C compile commands must be retained when qualifying a profile.

Concrete next task: implement the charged single-owner causal converter/buffer
with explicit grid-boundary ratio updates, bounded input/output service, startup,
underflow/overrun, Stop/cancel/tail and retirement tests. Then implement/qualify the
clock/buffer controller and native SDK composition before enabling Windows duplex.
Earlier fixed-resampling/stretch quality failures, native/installed/hardware/load,
localization and full P005/M2/F/Q/C/N requirements remain open.
