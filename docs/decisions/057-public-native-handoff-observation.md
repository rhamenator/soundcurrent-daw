# ADR 057: public native handoff observation before adapter changes

Status: accepted diagnostic scope, 2026-10-07 UTC.

Keep original native channel-delay failures and missing terms. Add bounded,
test-only public PipeWire listeners/API observations to an opt-in fixture while
preserving original callbacks, returned buffers, routes and sample oracles.
Prepare all observation storage before activation; audit outer and inner RT
segments separately and serialize only after join. Record unsupported IO and
overflow honestly. Private queue depth and post-process publication stay unknown.

Pin installed 1.6.2 source analysis. A passing new run with complete observation
coverage cannot resolve an older failure without causal evidence. Do not introduce
sample/time compensation, dependency forks or production changes on that basis.
See [scope, evidence and next experiment](../71-native-port-handoff.md).
