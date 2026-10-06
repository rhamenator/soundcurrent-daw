# M2c3: saved master layout, matrix and output intent

A session may now contain a stable-ID `MasterBus`: output layout, ordered sparse
track/channel matrix and its own output endpoint intent. The framework-independent
session model owns these plain values; prepared engines compile immutable copies.
Schema **1.3** stores the optional master. Versions 1.0–1.2 migrate with no master,
retain their existing routes/monitoring and never guess device assignments.
Strict field/number/UUID/layout/binding validation remains in force. The project
byte bound (4 MiB), 256 tracks/channels and 65,536 matrix entry limit remain visible.
The serialized byte bound may prevent saving matrices below the entry limit;
that limit does not imply that every maximal session can be saved.

Each source/destination pair is unique within its track lane. Finite linear gains
are admitted within ±64; zero means a silent route, negative means reversed
polarity. No pan law/downmix, normalization or omitted-track mapping is inferred.
Empty master lane lists can describe a stopped/unrouted project but cannot prepare
an audible graph. Removing a track removes its master lane in the same atomic
edit; Undo restores both. Adding tracks does not guess new channel routes.

## Desktop workflow

Stop playback, choose **Edit master layout and matrix**, choose mono/stereo/
discrete output and add/remove channel-pair rows. Track choices carry UUIDs, while
names and numeric input use the UI locale. Wheel edits require focus. Gain entry
retains 17 significant figures for unchanged doubles. Unlisted tracks are excluded explicitly. Apply
validates and submits one semantic edit; Cancel leaves the project untouched.
Changing the output channel count clears its old device assignments. Invalid
source/destination indices and duplicate pairs are refused visibly.

The current widget admits at most **4,096 entries**. Larger saved matrices remain
intact and available to the API/engine; the editor refuses Apply rather than
truncating them. A virtualized larger editor and extensive display/accessibility/
localization qualification remain required. This widget is not a general
bus/send/sidechain/VCA mixer.

Enable **Play project mix**, prepare and explicitly select the master outputs.
A saved matrix supplies the exact plan, including mixed source layouts. Its
output intents stay on the master rather than any inspected track. Missing or
ambiguous endpoint placeholders retain the existing passive restore contract.
Inspector selection does not retarget audio. Without a saved master the earlier
matching-layout preparation choice remains available. Matrix/identity changes
fault an incompatible prepared generation; output selections do not reconnect
active playback automatically.

`render-mix` uses the saved master when present, or an explicit all-track identity
plan otherwise. Native and offline processing still share the same prepared graph.
Monitor/room correction is not added to that default print path. Desktop export
still selects a track; a master export dialog workflow remains required.

## History and qualification

Master edits use the existing mixed 256-unit/32-MiB retained-payload history.
Structural patches retain both master values with their dynamic matrix/route
payload counted. Output patches merge ordered slots against the latest canonical
state and use the existing route history. Save captures an immutable accepted
prefix; reopen is passive. No GUI/serialization/routing edits enter RT callbacks.

[Evidence](../tests/results/M2/2026-10-06-master-matrix.json) records strict schema
and historical migration, invalid/duplicate/missing bindings, mixed structural/
route Undo/Redo and track removal, actual dialog/desktop Save-reopen/output
ownership, and independent stereo WAV sample comparison. Owned native 32-track
saved-matrix playback and output disconnect are also compared against source-coordinate/EQ replay. Physical devices,
native Windows GUI/audio, long-duration/deadline/memory/filesystem qualification
remain open, including the earlier native completion timeout and a new sink
clock-gap failure of unknown cause. A later isolated run passed exactly; neither
observation is waived by the passing run. The sink fixture now records its first
expected/observed clock position, duration and unavailable-input count without
callback logging.

Next characterize and diagnose native sink/graph clock gaps, then implement
simultaneous armed recording and overdub on one shared native
playback/capture callback, with timestamp/alignment/gap/recovery evidence. General
routing/PDC, all remaining recording/editing workflows, frozen F/Q/C/N coverage,
X004 imports, X005 monitor/print/portable profiles and all-Europe localization
remain in scope. The optional master state does not establish full DAW parity.
