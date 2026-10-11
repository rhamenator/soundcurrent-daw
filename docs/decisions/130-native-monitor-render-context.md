# ADR130: bind monitor output to actual native render facts

Status: accepted for prepared context and compiled wrapper; activation gated.

Use an inactive existing WASAPI stream's immutable SDK descriptor to bind the
prepared monitor output consumer and original render clock transport. Keep a
stable table alive before SDK construction and through join. One control binding
races one SDK end; unbound end refuses activation and retains original evidence.
Reserve whole context/table objects before transfer, with no new dependency,
driver, GUI route or schema change.

Publish original clocks before consuming a bounded bank prefix. Abort actual live
underflow or faulty output; preserve the untouched suffix. Only healthy finite
completion can certify terminal slack. Content Finish is distinct from actual
successful native release, guard/drain and Stop. Retain original end before clock
EOF; record bank consumption and native commitment in separate domains. Never
invent a joined SDK end, HRESULT, native/project mapping or successful drain.

Tests cover prepared failures, real binding races and composed capture/processing/
disk/render owners with explicit simulated SDK pacing and prior fits. These do
not establish native timing or installed parity. Keep the factory disabled until
original SDK outcomes have durable recording state, startup capacity/confidence
has an explicit policy and actual native failure/drain/retirement workflows pass.
[Checkpoint198](../198-native-monitor-render-context.md) records frozen evidence.
