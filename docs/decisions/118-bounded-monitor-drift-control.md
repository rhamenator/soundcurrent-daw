# ADR118: bounded monitor clock feedforward and source-ring feedback

Status: prepared original C++ component implemented; native composition and
quality/load/installed qualification remain open. Date:2026-10-11.
Extends ADR114–117 and the causal buffer without changing the frozen scope.

## Decision

Use original GPL-3.0-only C++20 code and the existing clock/buffer primitives.
No new dependency, driver, signing certificate or raw/project schema is needed.
The controller owns two preprepared clock windows per endpoint. Each consumes the
same original reliable readings. Every half of the admitted maximum span, restart
the older window from its last original trustworthy reading; the other window
keeps its fresh fit while the restarted one warms up. Restart preserves initial
reading, admitted configuration and sticky faults. It never fabricates a point.
Minimum span must fit within half the maximum span. Discontinuity, reset, identity,
frequency and unit validation still belongs to every clock primitive.

The newest valid fit is selected, then the longer window on equal end times.
Missing/delayed readings cannot refresh it. No fit means initial WaitingClock;
losing fits after committed processing means a terminal clock fault. Each endpoint
has independent age validation against an original control QPC timestamp.

Compute output/input ratio from capture frames, native render ticks/frequency,
output rate and original QPC endpoint differences. Subtract unsigned origins
before floating conversion. This ppm control calculation is intentionally not the
exact project-frame projection in ADR115. Propagate declared endpoint timestamp
uncertainty through the interval ratio, with a small floating-rounding allowance.
Insufficient confidence waits without changing control state. A clock ratio beyond
admission faults; it is never hidden by saturation. Uncertainty does not cover
unmeasured clock-model residuals, hardware delay or scheduling.

Add proportional source-ring occupancy feedback, filtered on the64-output-frame
grid with alpha=64/(outputRate*smoothingSeconds+64). Excess queued source lowers
output/input ratio so consumption catches up. Cap feedback, final ratio, and
per-grid change separately and expose each saturation. Defaults:±2,000ppm ratio,
1ppm/grid step,0.25ppm/source-frame gain,±250ppm feedback,0.25s smoothing.
This has no integrating windup state. These are provisional prepared control
defaults; the analytic reservoir tests do not qualify native stability or sound.

One pending proposal advances no grid/filter/slew state. The processing owner
must enqueue the exact event and commit it before servicing that grid. Retrying
the identical point returns the identical proposal. Different pending points and
bad/duplicate commits are refused. Queue pressure cannot justify PCM progress;
unresolvable pressure is an explicit EventRejected fault. This protocol is for
one serialized owner, not concurrent control/capture/render access.

## Alternatives and consequences

| Choice | Assessment |
|---|---|
| Nominal rate only | Cannot track separate endpoint oscillators |
| Occupancy only | Can control a queue but gives no trustworthy native rate/identity/age evidence |
| One long clock window | Rotation creates a confidence/warmup gap; reproduced by the new long simulation |
| Reset/reconstruct fits on callbacks | Unnecessary admission/exception path; preprepared window restart selected |
| PI feedback now | Adds tuning/windup policy without qualified native plant measurements; deferred |
| New clock/control dependency | Existing validated primitives suffice; additional license/build/RT costs unwarranted |

The controller charges4KiB of conservative owner/state credit off RT. Its
prepared operations are bounded POD/math work, without allocations, locks, I/O,
logging, SDK calls or exception paths. This is not a deadline guarantee. Retire
it only after the processing owner and future composed queues/SDK owners join.

Native composition must validate matching buffer/controller rates, generation,
output base, input capacity and ratio/step limits. It must establish clock fits
before admitting live monitor grids, without silently discarding raw recording
data. The default two-second fit span exceeds the buffer's nominal reservoir;
initial clock observation and monitor activation need an explicit native startup
protocol. Ring occupancy excludes filter/output/hardware buffering, so its target
is not total monitoring latency or durable recording alignment.

[Checkpoint184](../184-monitor-drift-control.md) records bounded evidence and the
next raw/output SPSC/native composition gate. Earlier processing-quality failures
and full F/Q/C/N, M2–M11, X004/X005, Windows and European localization remain open.
