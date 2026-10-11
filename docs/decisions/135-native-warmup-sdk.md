# ADR135: use finite native silence leases before monitored PCM

2026-10-11. Accepted optional SDK hook and portable lifecycle; monitor consumption
and versioned native-state migration remain pending.

An inactive render clock cannot supply the moving original observations needed
for unseeded monitor startup. Admit an optional pre-lease Content/NativeWarmup/Abort
hook with finite frame/current-QPC budgets. NativeWarmup acquires/releases actual
SDK SILENT leases and uses the ordinary client Start path; it never runs the PCM
callback or advances engine positions. Preserve the original stream reading and
separate current-QPC wait domain. Content latches Live; warmup cannot mask a later
underflow. Retain attempted/successful warmup separately from startup/guard/content,
including original failed acquisition/release results and missing-release refusal.

Do not serialize new evidence through schema1.0 by dropping fields. Refuse it until
explicit1.1 migration can retain it; old records remain readable without invented
observations. Compose monitor readiness consumption next, with exact descriptor/
identity admission, one clock publication per lease and terminal-state checks.

Scale the actual cached QPC frequency without a1GHz restriction: checked64-bit
multiply/divide for the common path and a fixed24-step modular fallback. Keep the
independent wait clock separate from original SDK timestamp fields. An independent
integer oracle covers conversion boundaries, representative frequencies and overflow.

Bounded fixtures qualify the portable lifecycle and old-format refusal. SDK cross-
compilation does not qualify native activation, installed audio or low latency.
Keep the native monitoring factory disabled until those dependent gates pass.
[Checkpoint204](../204-native-warmup-sdk.md) records exact source and remaining work.
