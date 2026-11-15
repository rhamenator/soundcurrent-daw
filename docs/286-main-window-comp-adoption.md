# Main-window review and Apply for saved comp jobs

Checkpoint 286, 2026-10-14. The complete frozen DAW goal remains active.

## User workflow

The Edit menu now opens **Review rendered transitions…**. The dialog lists saved
pending requests in pages of at most 128 items. Reviewing a selected request
loads and verifies its owned audio on the background controller. The reviewed
item displays its track and project-frame range. Review makes no project edit.

**Apply reviewed audio** performs a fresh background file/PCM verification, then
submits both fragments to the canonical project controller as one atomic edit.
Undo restores the original pair and pending request; Redo restores the adopted
audio and retained provenance. Save remains explicit. The applied request is no
longer offered for duplicate application after saving and reopening.

The dialog labels rendered transitions as experimental and asks users to check
for audible artifacts before final use. This is a review/apply workflow for
saved jobs. Creating a new rendered transition directly from the timeline is
still required; the existing ordinary timeline crossfade remains available.

## Ownership and threading

`VerifiedCompAssemblyAdoption` has a private constructor and retains its joined
resource reservation through the command queue. Preparation binds independent
selection/request references, verifies actual files and full PCM, prepares both
source records and preflights the resulting state. The desktop owner rejects a
raw `ApplyCompAssembly`, an unrelated memory-ledger token, a conflicting payload
or a token with the wrong project/root/opening epoch. It rechecks expected clips,
takes, source declarations and request references before adopting either side.

GUI actions and the control worker do not perform the file verification. The
assembly thread performs it, and the GUI polls immutable snapshots. Closing the
review cancels pending verification; an already accepted canonical edit can
complete and remains undoable. Window close waits asynchronously for controller
retirement. Reopening the same root invalidates the old review. The existing
unity/same-rate handle verifier is unchanged. See
[ADR 171](decisions/171-verified-comp-adoption-owner.md).

## Evidence

Source `7516856fbabfedfcbc57698d5ba3baf999e6b68a` passes the main-window suite and
the existing controller regression suite. CTest process 4000066 exited normally;
test time was 52.21 seconds and enclosing qualification 52.22 seconds. Source,
executable and static-library hashes remained unchanged. The
[index](../tests/results/M2/2026-10-14-comp-assembly-ui/index.json) records the
2,113-entry, 56,326,575-byte acceptance archive and its original process evidence.

- Six actual main-window cases, 87 assertions, use real nominal/span workers,
  owned assemblies, Qt widgets and the canonical project controller.
- Three successful workflows cover 44.1/48/96 kHz source fixtures, explicit
  Review/Apply, duplicate-Apply refusal, Undo/Redo, save/reopen, visible timeline
  refresh, consumed-request removal and export of reopened audio.
- Their 35,666, 49,152 and 16,384 exported stereo frames retain peaks above unity.
  All three WAV file hashes exactly match the independently retained checkpoint
  285 engine qualification. This is a whole-file comparison, including audio.
- Raw-edit and foreign-ledger submissions are refused without mutation. Changing
  a WAV after Review prevents Apply. Same-root reopening prevents stale Apply.
  Closing a review during a verification barrier leaves the project unchanged
  while GUI timers continue to advance.
- All six windows wait for project and assembly workers to close. The existing
  fifteen controller cases / 171 assertions still pass, including stale edits,
  cancellation, recovery, GUI progress and joined-memory release.
- Both suites together retire 24 source workers and six fixture processes.
  Six original PNGs and complete copied project/job/audio evidence are retained.
  The representative review and reopened-main-window screenshots were inspected.

Development tests passed before qualification. The final label build succeeded;
the broader dependency rebuild emitted an existing PipeWire codec indentation
warning, and CMake reported the existing dependency compatibility deprecation.
No warning-free whole-project claim is made.

Actual localization update and structural check completed after the UI edits:
1,062 source keys, 34 partial catalogs and 3,168 draft translations. No language
is newly native-reviewed or fully UI-qualified. Windows CI now builds and runs
this new main-window suite and retains its artifacts, but execution of this
revision on Windows is still pending.

## Boundaries and next work

Original CI 38636449748 at the preceding PR head `d2ec165` was still running at
this checkpoint. Its head remains unchanged until terminal APIs, full logs, all
original artifacts and screenshots are retained and audited. This checkpoint
is backed up separately; it is not an installed preview or binary release.

Next connect the timeline's requested transition to bounded background recipe
preparation, trusted helper identity, durable references, paired span execution
and final assembly. Make saving required job references an explicit user action;
do not publish request authority from an untrusted completion. Reuse the actual
pair/assembly controllers and the guarded Apply path added here.

Source-aware repeated comp/stretch regeneration, nonuniform/protected production,
spectral-spur/mixed-join/phase/formant/transient/listening quality, native endpoint
and installation qualification, the complete frozen M2–M11/F/Q/C/N/X004/X005
scope and all-Europe localization remain required. The test uses offscreen Qt
and file export, with no physical audio endpoint. Cooperative cancellation is
not an OS sandbox or a hard bound on stuck filesystem I/O.
