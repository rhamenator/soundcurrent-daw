# ADR123: preserve original clock readings through separate bounded routes

Status: portable handoffs and processing pump implemented; native SDK composition
open. Date: 2026-10-11.

Use independent fixed-capacity SPSC queues for capture and render clock owners,
with original POD readings and local generation/sequence envelopes. One processing
owner fits both sources. Whole-owner admission precedes payload initialization;
all owners join before settlement and retirement. No new dependency is needed.

| Alternative | Assessment |
|---|---|
| Call the controller from both SDK owners | Races a single-owner fit and processing state |
| Replace original timestamps with consumer time | Erases source age, delay and uncertainty |
| Use one queue with two producers | Violates its SPSC contract |
| Retry clock overflow as though nothing was lost | Hides a missing observation and uncertain fit |
| Use monitor ordinals for recording alignment | Conflates rate adaptation and original capture timing |

Overflow and source/consumer loss are terminal with original first receipts.
Diagnostic accepted prefixes cannot be fitted as healthy after fault. Missing
and delayed readings remain visible without refreshing accurate age. At most
four readings per source are pumped; `step()` checks terminal state independently.
Observations may refresh during pending output pressure while DSP and scheduled
ratio-grid progress remain blocked.

[Checkpoint191](../191-original-clock-handoff.md) records evidence and its serial/
concurrent fixture boundaries. Next qualify the combined disk/clock/processor
workflow with advancing time before native activation.
