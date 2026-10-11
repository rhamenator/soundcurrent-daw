# ADR122: explicit single-owner monitor composition

Status: bounded composition and independent Windows synthetic execution recorded;
native workflow open. Date: 2026-10-11.

Use an original GPL C++20 owner around the admitted fanout, reservoir/controller
and output bank. Reuse the pinned libsamplerate and existing resource/SPSC code;
there is no new dependency, license or driver. Disk recording stays independent.

Prefer one bounded unit of progress per call and a charged pending output slab.
Retain exact pending PCM on pressure; do not advance source/DSP/scheduled
controller grids until publication succeeds. Clock time is an explicit original QPC domain input, never
derived from monitor ordinals or raw duration. Validate freshness and original
fit confidence before live grids. Explicit real postroll governs final drain.

| Alternative | Assessment |
|---|---|
| DSP directly in a native render callback | Couples SDK ownership and processing/resampling state |
| Continue pulling while the output bank is full | Overwrites or loses already generated PCM |
| Use output frames as the recording timebase | Conflates monitoring rate adaptation and original timing |
| Start nominal monitoring while clock fits collect | Hides uncertain startup and can exceed finite reservoirs |
| Infer final output extent from raw frame count | Assumes alignment and delay mapping that have not been established |

Call original clock observations on processing only, through the bounded original
handoff in checkpoint191; native SDK publication remains open. Control transfers
ownership only after join. Actual concurrent synthetic
fixtures qualify the handoffs already present, not native SDK/deadline behavior.
[Checkpoint190](../190-bounded-monitor-processing.md) lists evidence and remaining
clock handoff, combined disk/native, EQ/mix and recording-mapping tasks.
