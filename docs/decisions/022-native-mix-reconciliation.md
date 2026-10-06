# ADR 022: shared native mix owner and per-lane revision receipts

Date: 2026-10-06. Status: accepted for M2c2.

Use `MixPlaybackRun` in the production native owner for both legacy selected-track
and explicit multitrack plans. Keep the same clock/buffer/fault gate, inactive
preparation, explicit native output selection and callback-before-worker shutdown.
The backend-free bridge selects a fixed prepared run pointer through `std::variant`;
no endpoint virtual calls or dynamic graph/model ownership enter its callback.

Canonical deltas resolve UUIDs to the prepared lanes outside RT. A complete model
bundle retains required receipt watermarks per lane. The highest global event
receipt is diagnostic; it cannot prove that all earlier events on other queues
were applied. Backpressure retains exact suffixes and latest canonical models.

The desktop first exposes the existing matching-layout identity plan and saved
output anchor explicitly. This does not introduce a master bus into the project
schema; a dedicated persisted master layout/matrix/output model and editor remain
required before the general mixer. Inspector selection is independent of the
prepared mix. Default selected-track behavior remains available.

Independent device owners per track would weaken clock/alignment guarantees and
increase native lifecycle/routing cost. Treating the maximum receipt as the whole
model revision would misreport application if an earlier lane were delayed. A
separate native/offline mixing implementation would duplicate processing semantics.
No dependency/license changes or equalizer checkout writes are introduced.
See [workflow and limits](../32-native-mix-playback.md).
