# Project-backed punch recording controls

Date: 2026-10-06. P004 / M2. Desktop and synthetic-controller implementation;
full frozen-reference punch parity remains open.

## Persisted state and edits

Project schema **1.4** adds `punchRecording` with stable keys `enabled`,
`startFrame` and `endFrame`. Values are desired project sample frames, before
declared input latency. The field is required in 1.4; keys, boolean type, integer
type, bounds and ordering are strict. A disabled empty range is valid. Enabled
recording requires a nonempty range. Turning punch off preserves its locators.
Integer serialization retains positions through INT64_MAX without float rounding
or dependence on the display locale.

Schemas 1.0–1.3 load with punch disabled and zero locators, preserving existing
project identities, routing, monitoring, media and master state. Old strict readers
reject new 1.4 files instead of dropping their settings. No older project is
rewritten just by opening it; Save uses 1.4 and the existing backup/publication
policy. Recording journals keep their existing schema and per-take raw alignment
and timing origins.

`SetPunch` is a control-thread transactional edit. Structural history retains
before/after punch settings, supports grouped undo/redo and refuses conflicts with
later state. It changes no track, media, processor identity, route or export range.
An invalid batch changes no canonical model or retained history. Valid edits flow
through the desktop's existing bounded command queue, dirty state, barriers and
asynchronous Save/reopen path.

## UI workflow

The recording panel provides **Punch recording** and **Set punch range…**.
The dialog accepts exact whole sample positions using locale-aware integer input,
with labels, keyboard buddies, accessible names, validation feedback and Cancel.
Applying settings queues the canonical edit; canceled or invalid input does not
dirty the project. The dialog refuses submission after its project identity or
Open epoch changes. Visible strings use the existing Qt translation context;
translated catalogs and European-language UI qualification remain separate work.

Enabling an empty range initializes a checked ten-second range at the playhead.
Punch uses shared project playback and the armed-track list. On initial enable
or project Open, an empty armed list suggests the selected track. Clearing all
arms afterwards keeps the list empty; preparation requires an explicit arm. Raw input and playback output routes still need
explicit selection after Prepare. Controls are unavailable during preparation,
prepared recording, recording, pending receipts or close. Changing canonical
locators through another control path retires an incompatible prepared recording
generation; its audio window is never changed concurrently.

The panel displays saved punch positions and the endpoint's actual admitted
playback end only while that generation is prepared or running; Stop/retirement
shows 'not prepared' instead of retaining a stale end. The existing
recording-duration control bounds requested playback;
punch locators must fit that requested range. Preparation uses the accepted
project barrier snapshot, supplies the desired window to latency-aware engine
preparation and admits any necessary bounded postroll. Selecting the unbounded
single-track backend with punch enabled fails explicitly. It cannot silently
record the full range.

## Controller and ownership

Endpoint preparation remains on the recording worker, creates no disk take and
does not activate native audio. The worker queries the endpoint's immutable
prepared end after its factory returns. For punch, missing or out-of-bounds end
metadata is an admission failure. The prepared end must cover requested playback
and can extend at most the engine's 60-second declared-latency bound.

After explicit Start, raw capture, full-block monitoring, per-lane origins and
independent punch-out use the existing prepared engine. Stop/join still precedes
receipt handoff. Grouped attachment preserves the saved desired locators and
places each clip using its own capture start and declared latency. No allocation,
GUI, disk, lock, logging or new virtual dispatch is added to native callbacks.

## Acceptance and remaining gates

Codec tests cover all prior schemas, exact Save/reopen, strict malformed fields,
integer limits, preserved disabled locators, transactional rejection and undo/redo.
Controller workflows use synthetic endpoints with real graph readers and disk
writers to check differing-latency postroll, per-lane origins and exact take length,
joined handoff, timeline attachment, incompatible locator retirement and refusal
before endpoint allocation for outside ranges or unbounded backend selection.
Desktop workflows exercise the dialog, cancellation, invalid input, dirty state,
undo/redo, Save/reopen, preparation, explicit routes, record/stop and grouped take
attachment. See [evidence](../tests/results/M2/2026-10-06-project-punch-controls.json).

Initial full Debug passes 29/29 in 26.04s. After layout, arm-clearing and retired-
end fixes, the four affected Debug groups pass in 7.98s; all 29 ASan/UBSan/LSan
groups pass in 70.03s; four optimized codec/controller/timeline/discovery groups
pass in 1.86s. Final Linux builds are warning/error free. Windows headless build
is compile/link evidence only. Original clipped feedback pixels are retained;
the final dialog passes text-bounds checks and rendered visual inspection. The
stale-project dialog guard is implemented but not independently qualified by a
separate fixture in this checkpoint.

Native owned-route punch and Windows runtime/Qt/device/installers need independent
acceptance. Declared synthetic latency is not measured physical roundtrip latency.
Tempo/beat locators, draggable timeline markers, manual punch, Auto monitoring,
record-stop/continue options, loop/take lanes/comping and overlapping-take audition
remain required. The original sustained native performance gate and all 21 prior
observations remain independently open. All 92 frozen acceptance/quality/reference
and F/Q/C/N contracts remain unpromoted; full DAW/import/profile/all-Europe scope
is preserved. No dependency, equalizer write, signing purchase or publication.
