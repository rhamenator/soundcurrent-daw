# Close requests and historical controller errors

Date: 2026-10-06. Scope: non-real-time desktop behavior; full goal incomplete.

## Reproduction

The GUI polls an immutable controller snapshot every 16 ms. A rejected edit can
already be published when the user clicks Quit, while the GUI has not displayed
that error yet. Previously the next poll treated every newly displayed error as
a reason to cancel closing. A clean project therefore stayed open without a
prompt. Dirty projects could lose the accepted close request in the same way.

The fixture publishes a rejected invalid parameter without processing Qt events,
checks that the canonical model is unchanged and the GUI still shows its prior
notice, then requests Close. At production revision `f2763e3`, the clean workflow
fails the bounded close await. Its exact execution handle terminates before
product edits or rebuilding. This establishes that event-order bug. It does not
establish the cause of the earlier retained sanitizer close timeout: that run's
original error serial, mode and snapshots are unknown.

## Fix

At an accepted close request, record its snapshot's error serial before committing
focused widget edits. Continue showing historical errors. Only a later serial
cancels an active request and clears its Save/barrier state. A stale snapshot
older than the baseline cannot cancel closing. Each retry captures a new baseline.
Existing recording finalization, verified attachment, accepted-edit barrier,
Save/Discard/Cancel and asynchronous worker retirement remain in order.

This changes GUI state only. There is no change to engine arithmetic, audio
callbacks, event queues, recording durability, project schema, identifiers,
dependencies or equalizer source. The portable Qt code has Linux execution
evidence here; native Windows execution remains required.

## Acceptance evidence

Three added workflows exercise:

- Clean close with an already-published, undisplayed rejection, without a prompt.
- Dirty Save/close with that rejection, exactly one prompt and exact saved model.
- Dirty Save/close with a new filesystem failure: the window stays open, the
  dirty model survives and the saved project is unchanged. After repairing the
  owned fixture, explicit retry prompts once more and saves the exact model.

All check historical error display and retirement of project, playback, recording,
export and recovery workers before the window is hidden. Failure injection uses
a directory at the temporary project's lock-file path; it works without changing
permissions or touching any user project.

Targeted workflows pass in Debug and Release. Five desktop groups (UI, project
controller, recording controller, export UI and export controller) pass in Debug
(7.46 s) and ASan/UBSan/LSan (15.11 s). The optimized desktop app builds. No native
audio callback or VM is started by these fixtures. Original native processing
library hashes remain unchanged. See
[evidence](../tests/results/M2/2026-10-06-desktop-close-error-baseline.json).

All 20 retained observations remain; this fix does not resolve the sustained
native deadline failure or prove the original close timeout's cause. All 24
borrowed inputs/heads and 92 frozen contracts still match. No parity status is
promoted. Next implement prepared punch ranges and sample-exact boundary splitting
in monotonic duplex capture, retaining the independent sustained quality gate.
Loop/take lanes/comping and every other full-product requirement remain required.
