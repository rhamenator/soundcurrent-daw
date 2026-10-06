# S6g desktop recording and recovery handoff

**Linux first-track recording/monitoring, live EQ/undo, verified attachment and manual recovery now have desktop controls. SLICE-001 still requires export and outstanding acceptance gates; full frozen-reference parity remains incomplete.** The framework-independent engine and raw media path remain separate from Qt. Equipment profiles are still an offline library/editor and do not yet process monitor audio.

## User workflow

Create/open a project, choose **Monitoring off** or **Monitor through track EQ**, then **Prepare recording**. Preparation publishes inactive ports and makes no take directory. Select every input and, for post-EQ monitoring, every output explicitly; no default is chosen. **Arm first track**, then **Record** (R). Stop recording or unarm to finalize and add the raw take. The input and monitor peak meters are colorized; monitor Off hides its signal rather than suggesting audible monitoring. EQ edits and scalar Undo/Redo affect monitoring, with desired/accepted/applied revisions kept distinct. Raw audio is unchanged by EQ. Save persists the attached take and current scalar edits.

The source inventory refreshes on the preparation worker. Removed selections return to the placeholder; core port admission rechecks IDs, serial, name and direction. The recording node's own ports are excluded. Input/monitor disconnect, rate/clock/buffer faults stop capture with explicit terminal telemetry and retain a valid prefix where possible. A disk failure retains its authoritative error and checkpoint. Numeric level calculations and status updates run outside audio callbacks. Physical speaker/microphone behavior is not inferred from synthetic testing.

This preview separates recording and file playback. Stop the existing playback preparation/transport before preparing recording. Prepare/Record are single-flight in the GUI; duplicated commands cannot replace an active take. Changing monitoring mode requires stopping/repreparing. There is no monitoring Auto mode, simultaneous overdub playback, multitrack arm, punch/loop/takes/comping or general clip undo yet; those remain required M2/M4 workflows, not excluded features. Input/monitor selections currently remain transient; complete portable per-channel routing intent and missing-device resolution remain open.

## Threading and parameter reconciliation

`RecordingController` owns its endpoint exclusively on a QThread, including creation, routing, disk-job activation, port inventory, native joins, disk finalization and recovery I/O. The GUI submits a bounded 16-entry non-RT FIFO and polls an immutable latest snapshot. Canonical session/gesture undo stays with `ProjectController` and its separate I/O worker. No Qt, disk access, joins or virtual dispatch is introduced into the native process callback.

Full-model following uses one explicitly coalescing slot. The worker admits only scalar/enable changes against the prepared identity/layout/rate/route/clip structure. Changed structure stops recording rather than silently binding different media or a project. One prepared bundle retains its exact unsubmitted suffix when the DSP queue is full. Its whole model revision becomes accepted only after every event is admitted; applied revision waits for the bundle's last generation-matched receipt. Newer models follow after that bundle. Intermediate audible partial-band application is possible and is not claimed to be an atomic DSP transaction. Canonical undo history remains independent.

Stop uses a priority epoch, invalidating old queued transport commands. The snapshot acknowledges that epoch only after the endpoint stops and joins. Interrupted setup cannot later activate old commands. Closing is a priority flag; final destruction joins defensively, while normal GUI close waits asynchronously. Existing filesystem/native APIs can block this worker; no forceful thread cancellation or bounded device/disk deadline is claimed.

A completed requested capture range retains the silent native route until explicit Stop/close, so graph destruction does not race delivery of the final monitor block. Real input/writer faults finalize/retire promptly. The production native owner still supplies raw-before-DSP copying and ordered callback-before-writer shutdown; [S6f contract](21-native-recording-owner.md).

## Pending takes and closing

There is exactly one pending finalized receipt. New preparation or recovery cannot overwrite it. The GUI submits it to typed journal/hash verification on the project I/O worker. Receipt root/project/asset identity must match; no speculative clip is published. Intervening EQ edits survive attachment. Success acknowledges the receipt only when its asset identity appears in the canonical snapshot. Attachment is dirty state and does not silently save.

A failed attachment leaves the receipt and original recording intact. **Retry adding take** resubmits verification. **Keep take for recovery** explicitly releases the pending handoff without deleting files. These actions wait until project I/O is idle. The fault status includes the stored job path; the recording status tooltip also provides it. A zero-frame failure before initial journal publication may leave an uninspectable orphan directory; discovery/cleanup remains open.

A modal-close guard prevents an unanswered dirty dialog from recursively enqueueing another barrier/dialog. The acceptance fixture leaves it unanswered across several timer turns before choosing Save.

Window close first requests recording Stop and waits for its acknowledged epoch and pending take resolution. It then queues the canonical accepted-edit barrier and presents Save/Discard/Cancel. Save happens after attachment; Cancel leaves the newly attached dirty take available; Discard preserves media files even though the project snapshot is not changed. Native/recording/playback and project workers all publish closed before the window disappears. A verification error cancels the close request, keeping the window available for retry/recovery. Project replacement is disabled while a recording job or handoff is pending.

## Manual recovery preview

Use **File → Recover recording…** to choose a capture job inside the current project's `media/` directory. Inspection and prefix hashing run on the recording worker. Show the verified frame count and duration and ask before copying. No confirmation means no recovery copy. A confirmed recovery creates a new UUID/job, preserves the original and submits the new finalized receipt through the same attachment path.

Recovery checks current project/first-track/rate/layout identity and refuses already-referenced source assets, cross-root jobs and zero verified frames. It re-inspects before copying and checks the result's frame count/sample digest against the preview; a changed source fails without attaching it, preserving any completed copy for inspection. Existing assets and the saved project remain unchanged on rejection. Copying is not forcefully interrupted once started; a completed result is retained through close. Beyond-checkpoint header frames are excluded by the core recovery contract.

This is manual job selection, not automatic startup recovery discovery, a general media importer or hostile-filesystem sandbox. Bounded discovery, malformed/zero-frame orphan presentation, media relink and backups remain required. Project directories and ancestors are owner-controlled, as in the existing storage contract.

## Evidence and remaining gates

[Result manifest](../tests/results/SLICE-001/2026-10-05-desktop-recording.json) separates controller/synthetic-disk/UI evidence, real owned native routes, sanitizers and Windows headless builds. The GUI failure tests exercise blocked finalization with multiple GUI timer turns, delayed Save, Cancel/Discard, immutable raw takes, verification rejection, explicit keep-for-recovery, preview/copy and save/reopen. A timer throughput threshold was corrected to event-turn observation; these fixtures do not measure a GUI latency deadline. Rendered offscreen evidence found and corrected status-label clipping.

Native qualification selects only owned virtual source/monitor ports and audits graph defaults/pre-existing links before/during/after. The fixture compares recorded raw samples with the source, and independent monitor samples with a private offline engine replaying actual edit/undo receipts. Monitoring Off and source disconnect have separate results. Any missing monitor tail on disconnect is recorded separately rather than certified as full coverage. No physical routing is used.

```sh
cmake --build .cache/build-desktop
ctest --test-dir .cache/build-desktop --output-on-failure
QT_QPA_PLATFORM=offscreen python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-desktop/sc-pipewire-recording-ui-fixture \
  --modes normal off disconnect
```

Run native fixtures serially after CPU builds/tests. Native ASan/UBSan/LSan uses the explicit diagnostic `PIPEWIRE_DLCLOSE=false`; the previous normal module-unload leak and overlapping-load timeout remain unresolved. This is not a production workaround or complete memory/deadline qualification.

Native Windows recording/playback/Qt, hardware alignment/rate/quantum/reconnect, memory locking, deadlines and combined load, physical filesystem/power-loss/>4 GiB RF64, automatic recovery discovery and general editing undo remain unqualified/unimplemented. All-European translation/native review and accessibility/HiDPI/X11/Wayland qualification remain open. No frozen parity row is marked complete. Next primary implementation task: S7 transactional offline WAV export from the shared engine, preserving the remaining M1/S8 and full-suite gates.
