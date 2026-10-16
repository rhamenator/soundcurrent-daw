# ADR 187: share completed exact source checks within one admission

Status: accepted after source-bound Linux Debug/Release qualification; native CI pending.
Date: 2026-10-14.

The repeated desktop Debug workflow exceeded its unchanged 30-second observation
limit on its second edit. Test-only timestamps place the timeout in final assembly
verification: spans completed near 17.2 seconds, publication near 26.5 seconds,
and verification remained active at 30 seconds. A bounded 90-second partial
Callgrind profile of historical verification recorded 494 WAVE validations from
span source admission, accounting for 63.67% of its recorded instructions. This
instrumented partial sample is not a production wall-time benchmark.

A source admission traverses both sides' raw, base, retained and nominal assets,
then their historical asset declarations. These roles can contain exact duplicate
Asset values. Reopening, decoding and hashing the same declaration repeatedly
inside one admission does not add a distinct declared dependency.

Retain pointers to completed checks only for the lifetime of one
`admitCompSpanSources` call. Compare the entire Asset value, including identity,
path, layout, extents, hash and companion evidence. Never deduplicate merely by
path, ID or audio hash. Add a declaration only after its WAV and all companion
files have passed. Each comparison still polls cancellation/deadline callbacks.
The maximum pointer capacity is derived from the admitted declaration lists and
charged to ResourceLedger before allocation. No global or cross-call cache exists.

All subsequent admissions start empty. Existing pre/post-render and pre/post-review
calls remain, with their full fresh file hashes, decoded extents, companion checks
and immutable-owned-file assumptions. Rendering math, source history closure,
worker identity, publication/adoption guards and deadlines are unchanged.

Verify that a later call refuses altered bytes after an earlier successful call,
that restored bytes pass, and that cancellation and resource refusal release
credit. Qualify existing corrupt-history, source change, worker, full PCM,
Undo/Redo/save/reopen/export and actual repeated desktop workflows in Release and
Debug, followed by native CI. Retain the original failures and profile.
