# M2b: desktop timeline and selected-track workflow

## Usable subset

The desktop now shows the canonical audio-track list and a zoomable, scrollable
clip timeline. Select a track by keyboard or list click; select a clip by its
keyboard-accessible dropdown or timeline rectangle. Unicode track names remain
plain text. Selection is transient UI state: it never changes project order,
revision, dirty state or saved identity.

Add mono/stereo/discrete audio tracks, rename/remove/reorder them, change a clip's
timeline/source/length range, split at an interior frame, duplicate, remove or
move it to a compatible track. Existing recorded assets can supply additional
clips. These controls submit the same stable-ID transactions as the developer
CLI and share the existing mixed Undo/Redo history. Raw media remains unchanged.
Structural controls require stopped/unprepared transport; selection and scalar
EQ editing remain available while another track is prepared. Structural Undo
can invalidate a prepared target; the owner rejects it safely and requires
Stop/reprepare, rather than executing a new graph inside its callback.

Exact frame fields accept nonnegative ASCII decimal 64-bit integers. Values above
2^53 are retained exactly in the model and fields; the visual axis uses doubles
and is approximate at that scale. Labels/tooltips use locale formatting. Full
locale-native numeric entry, translated catalogs and native-speaker/accessibility
qualification remain open. Unfocused clip/destination/media dropdowns ignore
wheel changes. This is an initial rectangle timeline, without waveform drawing,
drag/snap, fades, multiselection, take/comp lanes, tempo grids or automation lanes.

## Inspector and prepared identity

The EQ inspector and saved route/monitoring intent address the selected stable
track ID. Current audio adapters still process **one prepared track**. Preparation
uses an immutable accepted-prefix barrier plus its captured selected ID; a later
selection does not silently retarget the owner. Play/Record and route choices
for a different selected track remain disabled until explicit preparation.
Stop remains available independently of selection. Verified raw takes attach to
the prepared recording ID even if the inspector has moved elsewhere.

`sessionForTrack` supplies a control-thread-only immutable projection with that
track first for the existing single-track adapters. It never writes or saves the
projection and preserves the canonical session in `ProjectController`. Missing
IDs return no projection, rather than substituting another track. The inspector
caches projections by source snapshot/ID; audio owners normalize followed models
by their captured preparation ID before checking compatibility.

Playback validates the prepared track's layout, clips, referenced assets and
processor identities. Recording validates the capture track's layout/processor
identities. Changes to unrelated tracks, track names/order or export metadata do
not fault those owners or send unrelated EQ parameters. Project/rate/playhead,
target deletion and incompatible target processing changes still require
Stop/reprepare. Route/monitor preference changes remain passive until preparation.

Export defaults to the selected ID captured before its first barrier. The dialog
can choose another track explicitly; the final render request retains the
canonical immutable snapshot and chosen spec. A missing captured ID is reported.
Preparation never activates devices automatically. Simultaneous multitrack live
playback/capture/overdub is still required in M2c.

## Ownership and dependencies

`sc-ui-timeline` adds a Qt Widgets adapter using existing dependencies; the engine,
session schema (1.2) and callback code do not gain Qt or new libraries. All scene
construction, model copies, transactions and formatting run outside RT. The
existing 256-track/8,192-clip model limits apply; rendering thousands of rectangles
is not yet a load-qualified UI.

Selection handlers update fields without clearing their emitting scene during a
mouse event. List selection and scene-to-window reconciliation defer rebuilding
until the emitting input event finishes, including when a worker publishes a
new model during that event. The derived timeline destructor disconnects child-to-timeline
handlers before QWidget tears down children, preventing a scene teardown signal
from accessing destroyed derived members. A reentrancy guard prevents recursive
window polling while model updates reconcile selection.

## Acceptance and remaining gates

[Evidence](../tests/results/M2/2026-10-06-desktop-timeline.json) records actual Qt
keyboard/mouse edits, selection/order preservation, cross-track moves, split,
exact large frames, invalid entry, mixed Undo, raw hashing and Save/reopen. Fake
audio endpoints verify non-first-track preparation, passive selection, unrelated
EQ isolation and take attachment to captured IDs. The owned PipeWire roundtrip
is a separate real-backend regression; it does not establish multitrack native
playback or physical recording alignment.

P001/P008/P009/P086 retain their full reference acceptance and unverified F/Q/C/N
axes. Group-selection/phase rendering, fades/crossfades, complete history domains
and full multitrack capture remain open. Windows headless compilation covers no
Qt/audio runtime. Native Windows UI/devices, physical audio, deadline/load,
normal PipeWire module-unload memory and all-Europe translation qualification
remain required, along with X004 and the remaining X005 audio/portability work.

Next concrete task: prepare a Qt-free shared-clock multitrack playback/mix graph
used by live and offline rendering, with independent sample-sum/offset/EQ oracles,
bounded read-ahead/underrun reporting and RT ownership audits. Then connect
simultaneous capture/overdub to that clock; punch/loop/takes/comping follow without
reducing the M2 exit criteria.
