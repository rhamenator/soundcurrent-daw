# ADR 053: resource-admitted project track count

Status: accepted product/architecture target, 2026-10-06. Implementation pending.

The owner targets lower-budget studios, including studios with substantial
hardware, and requests an arbitrary number of tracks. Adopt no fixed product or
license ceiling on total project tracks. Any finite count remains subject to
trusted memory/IO/state admission; real-time execution has a separately measured
deadline budget. Physical channels/ports are distinct from project tracks.

Allocate dynamic canonical descriptors and prepared graph capacities off RT;
preserve bounded callbacks and safe graph retirement. Scale model, serialization,
reader/cache, UI/history, processing and recording together. Support defined
freeze/unfreeze, bounce and offline workflows without discarding original state.
Retain safe refusal and versioned limits for untrusted files and backend layouts.

The present256 track ceiling is an implementation gap, not the intended product
limit. Large synthetic workloads are regression targets rather than promises of
unlimited simultaneous processing. See [X006 and its exit criteria](../67-track-scalability.md).
