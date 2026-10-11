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

## Scoped-retirement integration

The PR104 stale-dialog/resource review correction is integrated here too. Linux
combined controller1662/UI283 assertions pass with the129-completed-job corpus,
exact resource return and held newer-render preservation. Original bff863a
publication evidence remains frozen at its original1520/276 scope; current-head
CI and native Windows qualification remain separate.

## Closed-dialog reopening correction

A dependent Windows desktop run failed while reopening recovery at test line136.
The closed dialog was still referenced until Qt processed DeferredDelete; an
immediate reopen could show that closing QObject instead of admitting a new scan.
A deterministic Linux regression leaves deferred deletion pending while the
worker retires, then requires a newer request ID and a different dialog. It fails
against the previous implementation and passes after the lifecycle correction.

On finished, stop the dialog timer, clear its automation identity and clear the
stored dialog reference only when it still names that dialog. Scoped backend
cancellation remains unchanged; cursor/file destruction stays on the worker.
The old QObject remains owned by Qt until normal deferred deletion.

Local stretch UI285 checks and both catalog gates pass (three tests,9.51s;
callback audit0, no audio). Qt Linguist refresh changes only the UI source hash;
856 source keys and all catalog bytes remain unchanged. Original bff/423/a84
acceptance and the original failed Windows log retain their original scope.
Fresh current-head native Windows checks are still required.

## Publication root admission

PR105 review found that removing the directory inventory also removed the
ApprovedMediaRoot constructor's lexical root validation. Restore its equivalent
absolute-path,4096-native-unit and embedded-NUL checks at the public publication
entry point, before encoding, credit reservation or filesystem access. The owned
directory/no-reparse checks, writer lease and per-manifest admission remain.

Direct API tests reject a relative path naming the owned project, a NUL-containing
path and an overlong absolute path as InvalidParameter. No selection directory
is created and resource credit returns to baseline. The portable fixture changes
only its own test process's cwd to an owned parent, restoring it on exit; this
works when Windows checkout/temp paths are on different drives. The original
Linux red case accepted the relative root; the corrected controller1675/UI285
checks pass, including all129 actual retained renders. Native current-head gates
remain required; no aggregate history scan or admission ceiling is reintroduced.
