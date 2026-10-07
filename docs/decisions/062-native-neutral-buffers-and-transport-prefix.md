# ADR062: native neutral buffers and exact joined transport verification

Status: accepted,2026-10-07.

Native observation62 proves that the adapter rejects an otherwise valid input
chunk with the documented `SPA_CHUNK_FLAG_EMPTY` flag. Correct the API interpretation:
retain its lease, expose a distinct Silence classification and provide one
preparation-owned read-only zero plane. Keep extent, permission, shape, unknown/
corrupted flag and lease-return checks. No native library is forked or replaced.

Qualify actual desktop recording through explicit owned ports, the production
native implementation and worker-side receipt observation. Retain originals before
source/binary replacement. Missing original buffer terms remain missing; successful
later runs cannot supply them retroactively.

Compare the entire exact joined transport prefix, with an independently continuous
sink and explicit callback clocks/origins. Record producer-removal successors
outside that prefix separately. Do not reinterpret a gap inside the prefix or
accept a shorter common prefix as successful live recording. Cancellation verifies
durable committed frames; written-but-uncommitted bytes remain unqualified.

No dependency, public state version, rate/quantum/default-device mutation or frozen
parity promotion follows. See [native desktop evidence and limits](../77-native-manual-panel.md).
