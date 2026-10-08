# ADR074: observe recording start before latency compensation

Status: accepted, 2026-10-08.

Use the existing opt-in public API observer with the production single-track owner
and an owned nonperiodic WAV source to distinguish native acquisition status,
callback clocks, published neutral media and genuine encoded silence. Keep the
observer, source generation and all disk/serialization work outside product RT
callbacks; observer storage is prepared and bounded before activation.

The current observations show a one-quantum published EMPTY prefix and an exact
later source suffix. Preserve original raw takes, including valid silence. Do not
fix alignment through amplitude admission, an assumed fixed callback count, or
post-hoc trimming. Missing original buffer terms stay missing. Existing declared
input latency and driver diagnostics are not interchangeable.

Implement input-latency observation and an explicit timing/alignment contract
next, with changing/absent latency and silent-source tests. No new dependency,
project schema change or full-parity promotion follows this bounded experiment.
See [evidence, reproduction and limits](../95-input-acquisition-observation.md).
