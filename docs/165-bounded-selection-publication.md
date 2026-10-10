# Publication independent of retained render history

This M2 continuation removes the artificial128-job/8MiB whole-directory ceiling
from new pitch/stretch selection publication. It builds on
[bounded recovery pages](164-resumable-render-recovery.md). Full DAW parity,
processing quality, native-project compatibility and localization remain open.

## Decision

Publish one immutable selection under the existing project-local writer lease.
Prepare/encode/admit at most512KiB of selection JSON, write and flush its uniquely
owned temporary file, poll cancellation, then publish to its operation-ID leaf
without overwriting. Publication never enumerates unrelated retained jobs.

The original whole-directory ceiling admitted a future all-at-once inventory.
Paged recovery now admits each page independently. A persisted aggregate count
or shard index would add a second state that could diverge after interruption;
there is no current requirement for it to authorize an independent immutable
operation-ID file. Keep the flat-v1 format and exact parent/helper digest binding.
Old manifest, raw audio, project and completed-job bytes are untouched by a new
publication. There is no index rebuild or format migration on the render path.

This does not provide unlimited storage. Existing per-operation source/input/
output, memory, protocol, deadline and single-worker admission remain in effect.
Write/flush/no-overwrite publication errors retain their explicit failure/owned-
temporary cleanup semantics. Existing cancellation polling and cooperative
publication exclusion remain. The128-entry/8MiB bounds continue to apply per
recovery page and to the explicit legacy strict all-at-once API.

## Acceptance scope

The new workflow test uses one saved Unicode project and129 actual sequential
parent/helper renders. Each completes and is independently verified without
editing that Session. Retained controller credit is measured after completion;
previous results are retired rather than accumulated by the controller.

It then adds20 owned512KiB interrupted-publication remnants (10MiB total) and
publishes one further valid, unrendered selection. Paged recovery inspects all150
entries, verifies each of129 completed jobs exactly once as Ready, classifies the
unrendered selection Incomplete, counts the remnants against page budgets and
preserves original project/manifest bytes. Explicit Review and Apply/Undo still
work for a completed result. Negative cases cover canceled publication, busy
publication lease, per-manifest oversize, exhausted resource credit and duplicate
operation/no-overwrite preservation.

This corpus qualifies the owned sequential publication/review workflow. It is
not a many-track simultaneous-throughput benchmark, physical-audio acceptance,
power-loss durability guarantee or perceptual stretch/pitch quality assessment.
Concurrent process contention, full-disk/fault-injected flush cases, installed
Linux/Windows UI and native Windows unit qualification remain separate gates.

## Next

Complete the protected paging/publication review and native Windows checks, then
qualify interrupted publication and storage-error preservation with owned failure
fixtures. Continue M2 editing/quality acceptance and the full M2–M11/X004/X005,
Europe-wide translation/review/UI and separate F/Q/C/N targets.
