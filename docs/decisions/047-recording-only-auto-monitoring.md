# ADR 047: recording-only Auto monitoring before continuous EQ

Date: 2026-10-06. Status: accepted for prepared audio recording.

## Decision

Add explicit `RecordingMonitor::AutoRecording`, persisted as `auto-recording` in
schema 1.6. Keep existing Off/Post-EQ identities and defaults. Accept earlier
schemas through explicit migrations, but never interpret the new identifier in
an older schema. Use existing stable-ID bounded Undo/Redo and accepted-snapshot
preparation; restore alone must not activate a backend.

A prepared Auto lane replaces file samples with current native input only within
its desired half-open project recording interval. Resolve monitored ordinals and
windows on the control thread. Per-lane raw capture ranges may differ because of
input alignment; do not derive logical monitoring from those delayed ranges.
Preflight all replacement views before any file cursor advances. Consume files
and retain reader error accounting throughout. Copy all selected native inputs
before clearing potentially aliased outputs, then process the existing track EQ
once over the whole block. Keep processor state and live parameter events across
selection boundaries. State layout and selection remain framework-independent.

## Alternatives and consequences

Monitoring the delayed raw window causes a late audible switch. Resetting or
preparing EQ at boundaries loses filter history, tails and smoothing. Switching
only at callback boundaries rounds punch locators. Suspending the file reader
while live prevents correct resumption and hides failures. Separate EQ instances
for file and input provide different history from a single continuously running
track and would require a distinct documented policy.

The setting explicitly names recording-only operation. Reference B's documented
record-running policy motivates the workflow; its manual, record-enabled and
tape-style policies remain required. Reference A Auto has additional reference
uncertainty. This is not a general armed/stopped monitor owner and does not
qualify full frozen-reference parity. See [contract and source links](../61-auto-recording-monitoring.md).

No dependency is adopted. This adds no callback allocations/locks or disk work.
Manual punch requires a subsequent bounded command and writer-lifetime design;
Windows native, physical latency, sustained deadline and localization qualification
remain independent acceptance gates.
