# Retained take lanes and transactional comp selection

This M2 foundation adds a portable model and control operations for retained
takes and comping. Desktop take controls and actual loop transport are the next
implementation work. This checkpoint does not establish complete P005/P006 or
functional/quality/content/native-project parity.

## State and timing

Schema 1.16 adds `Track.takeLanes` and nullable `Clip.takeOrigin`. Each inactive
lane has stable lane/group identities, a Unicode name, the original recording
transport range and its own clips. Its range uses project-rate frames before
per-track input delay; original device clocks and capture origins remain in the
unchanged raw-media journals. Lane clip positions may later differ from that
original range. Group members agree on the original range, and a track cannot
contain two lanes from the same group.

Arrangement `Track.clips` is the sole audible playlist. Retained lanes never
implicitly contribute to playback or export. A selected arrangement clip records
its source track/lane/clip identities, which survive moves between tracks and
save/reopen. Retained clips cannot recursively reference another take. Missing
or conflicting source identities and asset provenance are refused. Original
assets remain unchanged; existing stretch anchors preserve their raw source.

Both arrangement and retained clips use one encode/decode implementation for
processing, fades, exact source timing, playback rate and stretch/warp/context.
Legacy 1.0–1.15 projects explicitly default to no lanes or comp provenance.
Older schema numbers reject the new keys; malformed types, duplicate IDs,
overlapping retained clips and invalid geometry are refused. The test-only legacy
fixture helper refuses to discard actual lanes/provenance when constructing old
neutral fixtures; production decoding remains strict.

## Control operations and recording handoff

`InsertTakeLane`, `RemoveTakeLane`, `RenameTakeLane` and `MoveTakeLane` address
stable identities. Removing a referenced source lane is refused until the active
references are explicitly removed or replaced. `SelectTakeRegion` requires full
coverage of a positive half-open project range. It preserves arrangement audio
outside the range and copies only the chosen retained fragments inside it.
Exact source fractions, playback speed, fade anchors and stretch metadata survive
cropping. Kept fragments preserve their previous summation order. Repeating an
identical selection preserves clip IDs and produces no undo command.

A batch can select regions across linked tracks as one all-or-nothing undo/redo
operation. This uses existing structural history, with retained lane/clip/warp
payload included in state, staging and undo budgets. Lane inventories use these
trusted resource budgets; no new fixed total-take or total-track ceiling is added.
The existing edit batch and pending capture-slot limits remain bounded work/
backpressure windows, rather than claims about product track capacity.

`withManualTakeLanes` admits an already complete verified recording group into
inactive lanes, without changing the active arrangement. Explicit project
placement can differ from the recording transport range. Original per-track
delay/origin verification, media hashes and complete-group requirements remain.
Canceled or incomplete groups are not silently adopted. This function performs
no disk writes and does not automatically select or save a take. Preparation,
validation, indexing, model copying and history work occur outside callbacks.

## Actual local evidence

The new acceptance test records two distinct original mono sources through the
existing capture/writer stack, then selects A for the first half and B for the
second. Its independent sample oracle verifies shared live playback (different
callback partitions) and float WAV export. Inactive lanes produce silence;
original raw hashes remain unchanged. Interior replacement preserves surrounding
source samples/fades. One Undo/Redo and save/reopen retain both original lanes and
the selected arrangement.

An eight-track fixture selects linked sources in one transaction and verifies
each declared offset against an independent per-channel sample oracle. This is
declared geometry, not a microphone-group phase or physical-device qualification.
An independent rational check confirms `1/7 + 53*(44100/48000) = 48 + 937/1120`
and cropped fade anchors. Inventory, malformed state, missing references,
uncovered ranges and resource refusals preserve state/history. Sixty-seven
retained lanes serialize without a small fixed lane cap.

The final comp workflow passes 5,257 assertions, with zero measured callback
allocation/free/blocking-lock events. Existing manual recording tests pass
65,183 assertions in the final source-bound run across three repeated workflows, including actual complete
group retention and incomplete-group refusal. Sixteen targeted regression suites
passed during development (an earlier manual run reported 65,187); the final focused acceptance covers comping, manual
recording, structural edits and history resources. Builds use two jobs, nice 10
and ionice class 2/level 7. No audio endpoint or VM is used. Hosted native Windows
CI now includes the comp and manual-recording suites; its current-source results
are required independently.

## Next useful preview

The explicit retention and desktop lane/region/group controls are now described
in [checkpoint171](171-desktop-retained-takes.md); native and installed
qualification remain separate. Next implement real loop timing: monotonically advancing device/capture clocks,
separate looped project positions, boundaries inside callbacks, delayed postroll,
bounded upcoming slots and safe joined-worker retirement. Existing continuous
manual punch scheduling is not loop playback. Take audition, comp crossfades,
group edit workflows, native Windows/installed/UI qualification and the full
frozen-reference scope remain required. The failing stretch-quality baselines
remain open and are not promoted by these state/playback passes.
