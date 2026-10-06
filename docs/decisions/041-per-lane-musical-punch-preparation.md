# ADR-041: Prepare punch windows from per-track declared input latency

Date: 2026-10-06. Status: accepted engine preparation; full punch workflow open.

One shared raw window cannot place recordings with different input latencies at
the same desired timeline locators. Prepare separate raw `[B+L,E+L)` windows per
armed track, retaining stable track identity and declared latency. Extend playback
only for the required delayed capture, bounded by the existing latency cap, with
checked frame arithmetic. Preserve the original shared raw-window API and reject
ambiguous simultaneous raw/musical selection.

Each lane publishes its own first-capture origin; the aggregate origin remains
the earliest across lanes. Preflight all newly starting timestamps before any
publication in that callback. Continue full-block file/live monitoring, raw before
aliased output and the established native-join/disk-retirement policy. Keep schema
and checkpoint durability semantics unchanged.

Synthetic delayed-signal tests must prove timeline-aligned samples and attachment,
not merely agreement with derived raw windows. Persist desired locators and add
desktop/native/Windows acceptance next. Do not count this prepared primitive as
full musical/tempo, manual punch, Auto monitoring, loop/takes/comping or sustained
native qualification.
