# Create rendered transitions from the timeline

Checkpoint 287, 2026-10-14. The full frozen professional DAW goal remains active.

## Workflow

The timeline's Crossfade dialog offers **Use retained-take handles** and
**Render extended processed handles (preview)**, with an adjustable inner blend
limit. **Save and render** explicitly saves the current project and new job
references. Progress covers preparation, registration, save, source rendering
and final assembly. **Cancel rendering** retains owned recovery files.

Successful rendering leaves the timeline audio unchanged. **Review rendered
transition** selects the newly created job in the review dialog. **Apply reviewed
audio** uses the existing fresh verification and atomic paired edit. Undo/Redo,
save/reopen and export operate on the adopted result. Ordinary unrendered handle
editing remains available. The UI identifies the new renderer as experimental.

The screenshot review caught collapsed field labels in the existing form's
ignored-width layout. Preferred label sizing restores visible wrapped labels;
the tests now also check label geometry and the full Save and render caption.

## Ownership and failure handling

[ADR 172](decisions/172-owned-transition-creation.md) records the design.
Preparation and sidecar publication occur on the assembly worker, with bounded
joined resource reservations, fresh identities and trusted installed helper/current
executable hashes. A privately constructed token binds the canonical memory
owner, project, root, opening epoch and expected source state.

The canonical controller registers both references as one undoable transaction.
The GUI waits for its receipt and successful durable Save before launching source
workers. Two source children run serially under the existing supervisor; final
assembly follows verified completion. All file/PCM work stays off the GUI and
real-time callback. No project file can select an executable path.

Cancellation before registration does not change canonical state. Already
accepted registrations and saves can finish, as the UI explains. Reopening the
project or changing a source invalidates preparation; missing helpers and save
failures prevent child launch. Window close waits asynchronously for both
controllers. These are cooperative guarantees, not isolation from stuck OS I/O.

## Qualification

Source `ed351e6a740c24ce9071e01324a080db5248484c` is the implementation revision.
Five final suites pass in 78.52 seconds (CTest PID 4023798, exit 0).
Sources, executable and static-library hashes remain unchanged. The archive
contains 3,017 entries and 79,894,316 bytes. The retained
[index](../tests/results/M2/2026-10-14-comp-transition-ui/index.json) identifies the
exact source, executables, static libraries, child processes and acceptance ZIP.

- Eight new main-window cases / 174 assertions drive the real timeline dialog on 44.1/48/96 kHz
  fixtures, create fresh references and six actual source-render child processes,
  save them, assemble, review, Apply, Undo/Redo, save/reopen and export WAV.
- Rendering preserves clips/assets/playback sources until explicit Apply. The
  three exported WAV files exactly match this revision's existing Apply suite
  and the preceding Linux engine qualification. Floating headroom is preserved.
- Cancellation, same-root reopening, changed source processing, missing helper
  and injected save failure are covered. Failure cases launch no child; failed
  saving preserves both canonical references without changing audio. GUI timers
  continue during a deliberately held preparation worker.
- Six existing main-window Apply cases and fifteen controller cases run again,
  alongside the ordinary crossfade and Windows package-input tests. All windows
  retire their workers asynchronously.
- Actual localization update/check covers 1,090 keys in 34 partial catalogs,
  with 3,168 draft translations. Native-speaker review and full localized UI
  qualification remain open.

The application and affected tests compile. Linux staging includes the new
helper and rejects missing, stale or non-executable copies. Windows collection
includes it, and packaging requires matching native main-workflow identity.
Historical bundle validation retains its optional helper requirements. No new
installer, installed UI, physical audio or native Windows claim follows from
this Linux offscreen test run. Old main-workflow qualification receipts omit the
new helper and must be refreshed before making new installers.

## Original CI retained and audited

[Run 38636449748](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38636449748)
at prior PR head `d2ec165` passed all four original jobs. Terminal APIs, full job
logs, all five original ZIPs and their PNGs are retained locally before advancing
the PR head. The [audit](../tests/results/M2/2026-10-14-comp-transition-ui/original-ci/audit.json)
records native Windows and Linux schema 1.23 adoption, Undo/Redo/save/reopen and
live/export equality within each platform, plus fifteen controller cases on each.
The Windows bundle audit verifies fourteen deployment files, 1,639 corresponding
source files and both independent media builds against the reviewed binary pin.

Windows and Linux exports are **not bit-identical**: 81, 26 and 16 samples differ
in the 44.1/48/96 kHz fixtures, with maximum absolute differences of
2.384185791015625e-7, 5.960464477539063e-8 and 1.1920928955078125e-7. Original
files and measured differences are retained. This is an observation, not a new
cross-platform quality tolerance or a sonic-quality admission. Identifying the
specific arithmetic/source of those differences remains processing-quality work.

## Next work and limits

Run and retain native Windows CI for this new creation/Apply UI, then qualify
actual installation before distributing it. Next engine work is source-aware
repeated comp/stretch generation from adopted assemblies, with safe stale-source
refusal, followed by unsupported nonuniform/protected production and independent
spectral-spur, mixed-join, phase/formant/transient and listening-quality gates.
The full M2–M11/F/Q/C/N/X004/X005, native installation and all-Europe localization
scope remains incomplete. No equalizer repository, installed app, audio route or
VM was changed in this checkpoint.
