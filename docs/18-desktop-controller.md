# S6c asynchronous project controller and desktop editor

**Implemented project-editing preview; recording/playback/monitoring/export are not yet connected to the GUI. SLICE-001 and the full DAW remain incomplete.** The Qt-free engine/media targets are unchanged. The optional desktop adapter uses system Qt6 Core/Gui/Widgets; Qt Test is linked only by the UI fixture. Linux qualification used6.10.2. Qt6.4 is the declared API minimum, not a tested distribution/runtime claim.

## Control and I/O ownership

`ProjectController` is a desktop adapter with a control QThread and a separate I/O QThread. The control owner exclusively holds canonical `Session`, scalar `EditHistory`, gesture state and monotonic model revisions. GUI widgets submit a64-command, mutex-protected **non-RT** FIFO and poll a single immutable latest snapshot. No Qt/event/mutex operations are added to audio callbacks or the reusable engine.

There is one I/O job at a time. Create/open/save construct, validate, hash, flush and publish project files on the I/O worker. Parameter gestures and undo continue on the control owner while a save is blocked. The saved snapshot carries its captured revision; completing it records that revision and content, never silently marks a later edit saved. Dirty status compares canonical content to the last successful saved snapshot, so undo back to saved content is clean despite a newer command revision. Published read models never mutate.

New/open reject a dirty current project until it is saved; there is no implicit discard. Replacement blocks further parameter/undo mutations until its I/O result arrives. Failed load or save preserves the current model/root and typed error code. Invalid parameter values are validated before modifying gesture/history. A gesture token groups scalar updates into one undo record; final commits it, cancel restores it, and a different token/address commits the previous gesture first. This does not yet implement general track/clip/route/profile/enable undo or autosave.

GUI commands return Accepted/Full/Closing. Full queues do not overwrite accepted commands. There is no silent coalescing in this foundation. The GUI reports rejected submission; retry/coalescing and live-DSP command reconciliation remain the transport integration task. `completedCommands` counts successful command execution, not queue admission or durable saves. The I/O state and result/dirty fields distinguish those stages.

## Closing and failures

Shutdown uses a priority atomic flag, independent of FIFO capacity. The control worker joins the I/O worker and only then publishes `closed`. Save checks cooperative cancellation after the flushed temporary file and before publication. An already-published result is reported honestly; cancellation does not imply rollback of a completed save. Open/media hashing and filesystem flushes are not forcibly interrupted; the GUI remains responsive while shutdown waits. A user fixture hook can block indefinitely, but production has no such injected hook. Final destructor join is a defensive fallback; normal window closing waits asynchronously for `closed`.

A window close first queues a barrier behind all accepted widget edits, commits their gesture, and waits for that snapshot **before** inspecting dirty state. Save/Discard/Cancel then operates on the actual accepted prefix. Save-before-close is single-flight and waits for publication; Discard cancels outstanding I/O before its cooperative publication boundary. Cancel keeps the window and edits. This barrier avoids losing a freshly enqueued edit just because the previous displayed snapshot was clean.

Creation refuses an existing folder. If initial save fails/cancels, an owned newly created empty/temporary directory may remain; no automatic recursive deletion occurs. Writer-lock, media verification and snapshot backup behavior are inherited from the project-store contract. Error details currently include core English diagnostics under a contextual translatable message; complete translated error/help/installer catalogs and native reviews remain open.

## Desktop editor

The preview exposes New/Open/Save, scalar EQ undo/redo and Quit through keyboard-accessible menus. It edits the first track's frequency/gain/Q using stable addresses, spin boxes and gain sliders. A drag is one undo gesture; changes to a reordered/replaced band structure rebuild address bindings. It displays project dirty/busy state and track/clip metadata. It deliberately identifies its current project-editing stage; editing a parameter currently changes the model, **not a running audio generation**. The S6b engine ingress remains available for the upcoming transport owner to bind.

Scrollable content and available-screen initial sizing keep long band lists reachable. Wheel events edit only intentionally focused controls; otherwise they propagate for scrolling. Controls have accessible names. Qt contextual translation, Unicode strings/paths and locale-formatted numeric widgets are in place; no language is certified as delivered from that scaffolding.

The repository contains an original SVG icon and normal-user desktop entry, plus CMake install rules for executable/launcher/icon. No desktop entry was installed on the user's system during qualification. Qt/codec/compiler runtime packaging, installers, actual dock launch and Windows native GUI remain release gates. The preview's menu integration is not an installable professional DAW certification.

## Reproduction and evidence

```sh
cmake -S . -B .cache/build-desktop -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DSC_BUILD_DESKTOP=ON
cmake --build .cache/build-desktop
ctest --test-dir .cache/build-desktop --output-on-failure
.cache/build-desktop/soundcurrent-daw PROJECT_DIRECTORY
```

Desktop is opt-in; default headless builds do not find/link Qt. [Evidence](../tests/results/SLICE-001/2026-10-05-desktop-controller.json) includes separate controller/UI fixtures, all core groups, sanitizer results and a rendered1000×640 offscreen image with32 scrollable bands. UI tests exercise focused/unfocused wheel behavior, keyboard undo, slider gesture undo, locale comma decimal entry, persistent-band reordering and actual Save/Discard/Cancel dialogs, including a close immediately after enqueueing an edit. Controller tests hold storage publication blocked while later edits proceed, then verify the old saved content and newer dirty model; they also test cancellation, queue pressure and priority shutdown.

The Windows **headless** regression build passes. The new Qt controller/window has not been compiled or run on Windows; portable-looking source is insufficient evidence. Offscreen Qt tests also do not prove native Wayland/X11 accessibility, real monitor/DPI behavior or GUI-to-engine/audio latency. No audio device/route is opened by this preview or its tests.

## Next task

Continue S6 with a production transport/preparation owner: explicit native input/output inventory, record/stop/arm/monitor, clock-validated file playback/seek, retirement, immediate edit reconciliation, meters and recovery states. Keep media preparation/device joins outside GUI/audio callbacks, and qualify owned PipeWire routes before audible tests. Then S7 transactional export and S8 end-to-end acceptance. X005 equipment profiles/editor remains required alongside the full frozen parity, Windows and all-Europe localization work.
