# ADR125: reserve output before it can overshoot a finite end

Status: portable finite reservation implemented; native request/lifecycle integration
open. Date: 2026-10-11.

Reserve an immutable exclusive monitor ordinal on the single processing owner.
Cap kernel grids while input is still open, then accept bounded actual postroll
and automatically close after original EOF/route drain. This avoids asking a
late finishAt call to undo already-generated output. No new dependency or source
allocation is needed; the existing ring capacity is the hard postroll bound.

| Alternative | Assessment |
|---|---|
| Infer output end from raw frame count | Conflates resampling and original recording timing |
| Clip already-published output afterward | Cannot retract submitted native/hardware PCM |
| Close input as soon as output reaches the horizon | Can discard real postroll and hide later capture faults |
| Grow the postroll ring inside processing | Violates admission and real-time allocation bounds |
| Generate more to free capacity after the end | Overshoots the accepted output boundary |

Refuse late/conflicting/terminal reservations without mutation. Permit the same
end idempotently before closure. Preserve original clock/failure and raw evidence;
capacity loss is explicit. A native controller must separately admit intended
postroll and serialize requests through a bounded handoff. One finite reservation
does not implement punch/loop or a complete native Stop workflow.
[Checkpoint193](../193-finite-monitor-end.md) records proof and remaining gates.
