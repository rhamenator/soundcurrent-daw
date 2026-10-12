# ADR142 — Shared prepared EQ and initial native monitoring declaration

2026-10-11. Accepted for this bounded engine integration; desktop/native hardware
qualification remains pending. See [checkpoint213](../213-native-monitor-eq.md).

## Decision

Use the existing framework-independent PreparedEq/EqLiveDriver after conversion
and explicit channel mapping, before output-bank publication. Prepare and charge
all immutable state and scratch on control. Preserve original raw capture bits
and floating-point monitor headroom. One processing owner consumes bounded stable
parameter events and publishes applied receipts. Backpressure retries the already
processed block. Parameter timing is local monitor-frame timing, not project/raw
alignment. Zero algorithmic latency and until-silent IIR tails remain explicit;
a requested finite native end crops the monitoring tail.

Persist the initial declaration in canonical bounded monitor-eq.bin and reference
it by geometry/version/length/SHA-256 from auxiliary native state 1.3 and startup
admission. Do not reinterpret this declaration as recorded automation or printed
raw correction. Read legacy 1.0–1.2 and migrate explicitly without inventing EQ.
Publish the artifact before pending state; refuse malformed or mismatched state.

Use prepared private Windows high-resolution waitable timers for native-owner
polling outside callbacks; retain errors and post-join observed spans. Linux sleep
behavior and original manual recording worker remain unchanged. Current Windows
compilation is not native runtime qualification or a causal explanation of the
previous independent audio-flush stall. Preserve the unchanged acceptance gates.

## Consequences

The monitoring route and offline rendering share DSP implementation, parameter
IDs, smoothing and headroom semantics. Native state remains bounded and portable.
Real callback allocation/locks stay prohibited. Product automation, PDC, routing,
profiles, physical latency, UI/localization and Linux/Windows installation gates
still need separate acceptance. No dependency, driver or proprietary asset is added.
