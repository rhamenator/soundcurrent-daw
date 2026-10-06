# Saved recording-monitor preferences (S8b)

## Model and migration

Each track stores `RecordingMonitor::Off` or `RecordingMonitor::PostEq` in the
framework-independent session model. New projects and schema 1.0/1.1 migrations
default to Off. The schema 1.2 writer uses the stable strings `off` and `post-eq`
in `monitoringMode`, alongside the independent per-channel `monitorIntent`.
Translated labels and machine-specific numeric graph IDs are never serialized.

The decoder requires the exact fields for each version. Unknown modes, wrong
value types, missing 1.2 fields and future versions fail explicitly. Migrating
older projects preserves identities, routes, EQ, clips and media. Old projects
are never silently rewritten on Open; a later explicit save emits 1.2. The previous
snapshot preserves its original generation. Older DAW builds reject 1.2 instead
of dropping the setting.

## Edits and preparation

A typed control-side monitoring command addresses a stable track ID. It validates
before committing an unrelated EQ gesture. Changed values join the bounded
256-item semantic EQ/route Undo/Redo history; a no-op creates no revision or
history item. Invalid/stale commands preserve model and gesture state. Save uses
its accepted snapshot; changing the mode during disk work leaves newer edits dirty.

The desktop dropdown follows canonical state on revision/error/project-epoch
changes, including Undo and reopen. Its text is translated and wheel changes
require focus. It is editable while the recording owner is idle; prepared/active
graphs keep their captured mode. Undo can change the saved preference while a graph
is prepared; a visible status message discloses the mismatch and asks for Stop
and preparation. Restoring a project does not prepare, arm, connect or record.

**Prepare recording** first admits a canonical-controller barrier behind pending
edits. The retained immutable session/root/revision receipt supplies the mode and
EQ to the recording worker. Changing the dropdown and immediately preparing must
use that accepted prefix. Duplicate pending preparation is refused. Stop, unarm
or close clears the pending GUI preparation before requesting native stop. Disk
jobs still begin only when an explicitly armed, fully routed recording is started.
No callback, native parameter queue or DSP code is changed by this state addition.

## Acceptance and limits

[Evidence](../tests/results/SLICE-001/2026-10-06-monitoring-preferences.json) separates
core validation/migration/history, asynchronous controller/save-prefix tests,
actual desktop controls and owned native record/save/relocate/reopen/export.
The native reopen selects its monitor mode from saved state, with no manual
fixture override. Reused equalizer inputs are checked again before qualification;
this milestone does not adopt unrelated Windows equalizer routing/installer work.

Only Off/Post-EQ first-track monitoring is implemented. Auto monitoring, simultaneous
overdub, multitrack monitoring, latency/alignment, monitoring-only equipment
correction and the control room remain open. Native Windows execution/GUI/audio,
physical audio, normal PipeWire module-unload memory, load/deadline, general
interrupted-recording discovery, >4 GiB and filesystem/power-loss qualification
remain required. No frozen parity row or delivered language is marked complete.

## Next implementation task

Add bounded asynchronous discovery of interrupted/unattached recording jobs in
an opened project. Present candidates without automatic copying or deleting,
revalidate the selected verified checkpoint, retain the existing preview/consent/
copy workflow, and avoid reoffering already attached/recovered sources. Exercise
corrupt/foreign/active jobs, cancellation and close with owned fixtures. Continue
M2 multitrack foundations after this recovery step while independent platform and
physical qualification stays open.
