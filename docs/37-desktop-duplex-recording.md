# M2d3: desktop armed recording and grouped take admission

The Linux desktop now prepares simultaneous raw recording alongside the saved
project mix. `RecordingController` owns the production `PipeWireDuplexRecording`
on its worker. Canonical project state, media verification and history remain in
`ProjectController`; the framework-independent DSP/capture ownership is unchanged.
This advances a bounded M2 workflow; full M2, Windows and frozen-reference parity
remain incomplete.

## Desktop workflow

1. Create/open the project. Configure the saved master layout and explicit matrix
   for mixed layouts; the fallback identity mix requires matching track layouts.
2. Set each track's saved monitoring mode using its inspector: Off or Post-EQ.
   These are preparation-time policies; live EQ edits remain immediate.
3. Enable **Record armed tracks with project playback**. Check the tracks in the
   scrollable arm list and choose a finite recording range in seconds (1..86400).
   The default is 600 seconds. The fixture/automation API can supply an exact
   positive frame bound separately; it does not offer an exact-frame UI editor.
4. **Prepare recording** captures the accepted project-command prefix. It creates
   inactive packed input/master ports without starting writers or audio. Arm/range
   edits are disabled until Stop. Structural changes stop the immutable generation.
5. Select every input and project output explicitly. Inputs are labeled by captured
   track/local channel; selections save per-channel input intent. Output selections
   save dedicated master intent (or the fallback output anchor). No default route
   is inferred. Changing inspector selection cannot retarget the prepared arms.
6. **Record** starts all writers before native callback activation. File lanes and
   armed live lanes share one admitted clock. Every armed input is recorded raw;
   monitoring Off does not print or erase the take. File playback remains audible
   through the project output even when all armed monitor modes are Off.
7. **Stop recording** joins the native callback owner, reader and every writer.
   All successful receipts form one pending verification group. Each finalized
   journal, identity, extent, inactive ownership and media hash is verified before
   any canonical attachment. One Undo/Redo detaches/reattaches the whole group;
   intervening EQ/route edits remain intact. Explicit Save persists it.

The legacy selected-track recording workflow remains available when the multi-arm
mode is unchecked. Neither workflow enables physical inputs/outputs automatically.

The arm list shows captured/written frames and colors failed lanes red, with the
retained job/error in the tooltip. The overall meter reports actual project output
peak. Raw peak metering is unavailable in the duplex bridge and is hidden in this
mode; a made-up zero level is not displayed. Control/meter formatting remains off
RT and uses Qt translation contexts, locale numbers and stable UUIDs. This is
translation infrastructure, not all-Europe translation or native-speaker/UI proof.

## Identity, ownership and event receipts

Preparation captures the full canonical session through the existing barrier,
then keeps armed IDs, layouts, input packing, master plan and range immutable.
The input total is at most 256 channels, and the owner admits every pool/spec
before allocation. One inherited fair file reader and one raw writer per arm run
outside audio. No GUI/disk/virtual calls/allocations/blocking work enters process.

Live scalar/enable edits use the immutable graph's track ordinals. A bundle retains
its unsubmitted suffix on queue Full; its whole revision becomes accepted only
when every event is admitted. Applied revision requires each participating lane's
generation-matched acknowledgement. A higher receipt from another lane cannot
acknowledge a held lower revision. Reordering canonical tracks preserves captured
IDs and prepared ordinals. File-lane EQ edits also follow the canonical model.

The GUI/control slot may coalesce a latest full model, while accepted bundles
finish their existing suffix. Route/monitor changes are saved intent for the next
preparation. Relevant clip/layout/processor/master/project/rate/playhead structural
changes stop this generation and retain joined raw prefixes for admission/recovery.

## Grouped verification and failure behavior

`AttachRecording` accepts exactly one legacy receipt or one 1..256-receipt group.
All shape/identity edits are staged on a copy. A separate I/O worker verifies each
finalized inactive journal and every proposed media asset before the control worker
replays the attachments against the latest canonical session. One history adoption
publishes the whole group. Duplicate IDs, ambiguous/empty/overlong groups, one bad
hash or a missing target during verification reject every attachment. No speculative
assets are published; raw files are preserved. Scalar and route edits made during
verification remain canonical and are retained by take-group Undo.

A failed writer does not discard other successful takes. Successful receipts are
verified as one group; failed lane diagnostics/jobs remain available for separate
checkpoint review/recovery. A failed group verification offers Retry or Keep for
recovery, preserving every original file. Keeping means retaining media outside
canonical history, not inventing a successful recovery result. The existing scanner
and manual recovery identify a matching track anywhere in the full project.

A writer constructor may create a directory before initial-journal failure.
`DuplexRecordingRun` retains that existing path and the original activation error;
all earlier writers join and audio is never activated. A partial directory without
a valid journal is diagnostic material, not a verified recoverable take. Subsequent
per-lane errors cannot replace the controller's initiating activation/structure error.

Close with active arms first requests Stop and waits visibly for all writers and
pending group verification. Save/Discard/Cancel resolution uses the resulting
canonical state. A new preparation cannot overwrite an unacknowledged group.
No default/hardware/rate/quantum change is requested by this workflow.

## Evidence and limits

See [the evidence manifest](../tests/results/M2/2026-10-06-desktop-duplex-recording.json)
for exact source/log hashes, test sequence, failures and native observations.
Tests cover grouped all-or-nothing verification, concurrent scalar/routes, one
Undo/Redo, save/reopen, held per-lane acknowledgements and bounded queue retry,
canonical reorder, finite partial blocks, 32-arm common raw timing, partial disk
failure, activation rollback, pending results and active multi-arm Save/close.

The opt-in Release native GUI fixture uses three or 32 owned source planes,
production desktop/controller/native ownership, an existing unarmed file and an
independent stereo sink. It checks a five-second 240000-frame range, exact raw
source coordinates, independent offline EQ replay at actual applied frames and
float64 matrix accumulation, group Undo/Redo and Save/reopen. Host callback
allocation/free/mutex auditing and bounded elapsed timing are retained. These
finite owned routes do not qualify physical latency, long duration, background
load, every hardware buffer configuration, native sanitizer unload or deadlines.

Windows core cross-compilation is separate from native execution and Qt/UI
qualification. Monitoring Auto, punch/loop, take lanes/comping, fades/warps, PDC,
process-kill/disk-full/multiple-filesystem tests, X004 imports, X005 monitor/print/
portable profile integration and all-Europe coverage remain required. Historical
native clock/sink/completion and concurrent UI observations remain open; a new
successful run does not establish their causes or waive their gates.

Next: **M2d4 declared-duration recording qualification**. Build the reproducible
32-track ten-minute synthetic source/hash/timestamp/save-reopen workflow required
by P001, including bounded disk/cancel/failure evidence. Then qualify the separate
30-minute declared native device/load/alignment gate; coordinate physical routing
when necessary. Keep punch/loop/takes/comping and the rest of M2 in the backlog.
