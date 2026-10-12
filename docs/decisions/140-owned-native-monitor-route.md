# ADR140: own prepared native monitoring storage as one admitted group

Status: accepted, 2026-10-11. GPL-3.0-only; no dependency added.

## Context

Prepared native streams expose actual device descriptors before activation.
Existing capture/render callback contexts borrow queues, processing and startup
cells. Independent ad hoc construction risks inconsistent geometry, duplicated
memory admission and premature retirement as the native factory is connected.

## Decision

Derive immutable route plans from actual descriptors and explicit route options.
Validate format, FIFO priming, startup clock span and capture postroll before
allocating banks. Different channel widths require an explicit canonical matrix.
Keep original raw capture untouched and retain native/QPC/monitor/project domains.

Own the prepared components in nonmovable storage with dependencies declared
before borrowers. Acquire one aggregate lease before bank allocation; include
all retained matrix copies and conservative fixed storage. Preserve missing
native owner outcomes and queued raw prefixes during post-join settlement.
Keep SDK streams/bindings and worker/disk lifetime ownership in the next layer.

## Alternatives and consequences

Per-component ledger admission can fail after partially allocating a group and
does not express a single route budget. A mutable public configuration bundle
allows identities or widths to diverge after validation. A monolithic SDK/DSP/disk
constructor would obscure the existing preparation/activation boundary.

The selected layer retains these boundaries without new libraries or SDK calls
in processing. It has explicit caller join obligations until the owned factory
is implemented. It does not by itself enable a native desktop workflow, add
monitoring EQ or support every FIFO geometry. Qualification and next work are
recorded in [checkpoint211](../211-owned-native-monitor-route.md).
