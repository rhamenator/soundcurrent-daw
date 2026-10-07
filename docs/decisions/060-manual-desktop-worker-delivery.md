# ADR 060: serialized manual transport worker and result delivery

Status: accepted, 2026-10-07. GPL-3.0-only. No new dependency.

Manual engine/native recording supports repeated windows; desktop still uses the
fixed-take controller. Disk consumers/native joins need a serialized owner and
Cancel must remain meaningful while finalization is blocked or a core result is
awaiting transfer.

Add a framework-independent `ManualRecordingController`: worker-only endpoint
seam, 16-command FIFO, 64 retained control completions, 64 aggregate in-flight/
application audio acknowledgements and eight aggregate engine/result slots.
Reserve before admission; distinguish GUI admission, worker completion and actual
audio application. Reject stale generations. Preserve immutable results across
asynchronous Close until explicit acknowledgement. Shared monotonic Stop/Cancel
does not expose native/disk ownership.

Cancellation updates only results still owned by the engine. The acquire
observation in `takeGroup()` commits its next handoff; later requests cannot
rewrite that receipt. Preserve errors/independent durable media; canceled groups
refuse automatic/partial attachment. Observe Cancel during/after Stop and before
handoff, including through the native wrapper. Canonical history/Save and the live
prepared graph remain independent.

This adds a worker component and Linux adapter, without claiming actual-widget,
native-controller, Windows-native or sustained qualification. UI parameter
following, group adoption/recovery/Close and platform qualification follow.
See [contract](../75-manual-desktop-worker.md).
