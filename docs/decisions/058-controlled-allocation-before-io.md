# ADR 058: establish startup causality before changing acquisition

Status: accepted controlled mechanism checkpoint, 2026-10-07 UTC.

Use public mainloop buffer notifications to extend an owned negotiation window
without blocking audio or altering private PipeWire structures. Retain one normal
getter run and one explicitly declared counterfactual that defers the selected
unready output query. Keep actual clocks, source/received waveforms, durable raw
media and all independent gates. Freeze failed originals before diagnosis.

Allocated buffers alone do not establish publication readiness. The controlled
normal getter has a persistent one-cycle channel delay; deferral prevents it under
the unchanged signal/origin contract. This justifies production readiness-aware
acquisition, while historical cases with missing IO terms remain unproven.

Future tracing must retain logical channel identity when an acquisition is deferred
and distinguish it from an SDK call. Returned buffer capacity and lifetime must be
certified before callback views. Do not introduce time compensation, private queue
patches or backend forks. See [evidence and next implementation](../72-controlled-native-startup.md).
