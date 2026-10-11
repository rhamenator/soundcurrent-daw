# ADR120: typed output slabs between processing and rendering

Status: original prepared transport implemented; native composition and Windows
execution remain open. Date:2026-10-11.

## Decision

Use original GPL C++20 code, existing SPSC queues and resource admission. No new
dependency, license or driver is needed. Allocate and touch all slabs off RT,
retain one partial consumer slab, and return its slot only after complete copying.
Use generation-bound integer monitor ordinals and finite float32 payloads with
headroom. Capture stamps cannot stand in for this different timing domain.

Full output storage is nonmutating retryable processing backpressure. The producer
must retain the actual DSP output pending acceptance; no replay, drop, zero padding
or DSP reset can hide it. Render pulls have a fixed admitted frame limit and an
entry queue snapshot, protecting bounds even when production continues.

Producer and consumer faults have distinct immutable cells with separate writers.
This avoids a concurrent composite first-fault writer or invented cross-owner
ordering. Receipts expose phase separately from available prefix; native policy
must distinguish diagnostic draining from healthy playback. Actual underflow is
an explicit rendering decision based on activation/deadlines, not an empty-queue
guess. Cancel discards only monitor backlog after joined settlement; raw capture
has its own independent prefix and persistence contract.

| Alternative | Assessment |
|---|---|
| Reuse capture timestamps for output blocks | Conflates native capture and resampled monitor positions |
| Shared direct DSP buffer in render callback | Couples SDK callback timing to processing ownership |
| Unbounded drain until producer pauses | Violates bounded callback work |
| Insert zeros or replay on temporary Empty | Conceals genuine scheduling/activation failures |
| Blocking wait for queue room | Violates prepared processing/render constraints |

[Checkpoint187](../187-monitor-output-transport.md) records actual two-owner
Release/sanitizer/race evidence. Next is original raw metadata persistence and
complete bounded processing/native owner composition, with matching configuration,
clock startup, genuine failure/shutdown, quality and installed-platform gates.
