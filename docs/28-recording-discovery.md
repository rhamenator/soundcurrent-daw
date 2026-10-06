# Recording-job discovery and recovery (S8c)

## User workflow

Opening a project starts a passive metadata scan on a separate I/O worker. The
recording panel shows a count and **Review recordings… / Refresh** controls; the
File menu also offers **Find recoverable recordings…**. Scan again after verified
take admission or an idle/faulted stored job changes. Discovery neither prepares
an audio device nor arms, activates, copies, deletes or edits the project.

The screen-sized list scrolls. Rows distinguish checkpoint metadata needing audio
verification, legacy jobs with unconfirmed writer activity, active writers, empty
checkpoints, different project/track/rate/layout, invalid journals and sources with
a matching attached recovery copy. Frame counts in this list are explicitly
**claimed** checkpoint counts. They are not audio verification evidence.

Selecting **Review audio…** uses the existing recording I/O owner to read and hash
the committed audio prefix, validate format/identity/rate/layout, then show an
explicit recovery question with default No. Active modern writers are refused.
Legacy jobs disclose that activity is unconfirmed; only the verified snapshot
prefix can be copied. Accepting re-inspects the exact preview before copying to a
new UUID/job and verifying the result. Typed take admission independently checks
journal/media/hash evidence before attaching the raw non-destructive clip.

Canceling the list/consent leaves the project and source intact. Closing dismisses
both dialogs, cancels outstanding scan/recovery work at I/O boundaries and
waits for all five owners’ closed receipts before final window closure; controller
destruction joins their threads. Global Stop/Shift-Space and the Stop button now
cover recording as well as playback; they also clear pending recording preparation. Recovery verification/copy can be stopped between reads.

## Core and bounds

`discoverRecordings` is Qt/device independent, runs off RT and reads at most the
configured directory/job budgets. Defaults: 8,192 directory entries, 512 capture
jobs; maximum configurable bounds: 65,536 / 4,096. Zero/out-of-range limits fail.
Jobs are sorted after bounded enumeration. Limit exhaustion is disclosed as an
incomplete list; no completeness claim is made for excluded entries. Headers are
limited to 16 KiB with existing strict depth/type/version/identity validation.
Diagnostics are capped at 512 bytes and 16 metadata warnings. No audio is streamed
by the metadata scan. Missing media directory returns an empty result without
creating it. Linked/reparse jobs and lock files are refused.

Attached UUIDs are skipped. Optional attached recording journals are only hints:
identity, project, layout/rate/frame extent must match the saved asset before a
`recoveredFrom` link is used. A recovery chain requires matching track, timing,
layout/rate, latency, extent and sample digest at each link. Bounded lookup/visited
sets and cancellation prevent cyclic chains from looping forever. Sources remain
visible and explicitly reviewable; a metadata hint never authorizes deletion or
attachment. Missing/unreadable optional journals yield bounded warnings and can
leave a source available for review. Missing/truncated/changed audio is refused
by the later streamed verification, even with a valid metadata header.

New `CaptureWriter` jobs create an empty owner-only `writer.lock` and hold an OS
lifetime lease while their audio/header can still change. Linux uses nonblocking
exclusive `flock`; Windows uses exclusive file sharing. Read leases do not create,
write or remove a marker. Busy means active; a released existing marker confirms
cooperative inactivity; an absent marker means legacy/unconfirmed. Finalization
or failure closes audio before releasing its lease. Process death releases the OS
lease while leaving the checkpoint/marker available. An empty marker file alone
does not establish activity. These are cooperative locks in owner-controlled
project directories, not a sandbox against hostile directory/inode replacement.

Inspection hashes in 1,024-frame blocks with cooperative cancellation boundaries;
copying also checks between blocks and at writer/final-asset-hash boundaries.
A canceled copy can leave its uniquely owned partial or completed unadmitted job
for subsequent discovery. The original is preserved; canceled files are not
silently deleted. Blocking filesystem calls must return before cancellation or
shutdown can finish. Entry/memory bounds are not hard filesystem time guarantees.

## Threading and stale work

`RecoveryController` owns its own QThread and one latest-request slot. A new scan
supersedes queued work and cancels the previous request at worker boundaries.
Results carry root, project epoch and request serial; obsolete results are not
published. Snapshot exchange uses a non-RT mutex. It owns no audio node, graph,
parameter queue or callback. Recording preparation/telemetry stay on their own
owner. Metadata results are restored only for the current project epoch; list
acceptance rechecks current root/epoch before verification.

Automatic rescans follow Open and asset changes; idle/faulted job changes also
trigger them. Manual Refresh handles external changes in a currently open project.
There is no filesystem watcher or background polling of all files. GUI text uses
translation contexts, plural counts and locale formatting; diagnostics are plain
text and path tooltips escape markup. No translation delivery is implied.

## Qualification and remaining scope

[Evidence](../tests/results/SLICE-001/2026-10-06-recording-discovery.json) separates
core bounds/activity/corruption/legacy/copy-chain tests, actual owned SIGKILL
writer recovery, latest-slot/blocked-I/O/close controller fixtures, actual GUI
selection/verification/consent/cancel/reopen, and native owned recording/export.
Windows compilation is separate from native execution/GUI/audio qualification.

This remains part of the first recording slice. Autosave/edit journals, project
snapshot-recovery UX, missing-media relinking, permissions/disk-full/power-loss,
>4 GiB, native Windows, physical alignment, load/deadline and normal PipeWire
module-unload memory gates remain required. Metadata classification is not a
processing-quality, bundled-content, native-project or frozen-parity certification.
X004 imports, X005 audio/profile portability and European language delivery remain
open.

## Next implementation task

Start M2 multitrack foundations: stable track/clip editing commands and undo,
track selection/timeline, bounded prepared playback graph for several audio
tracks on one clock, and simultaneous playback/capture overdub. Use deterministic
multi-track audio fixtures and explicit per-channel routes, preserving raw takes,
shared live/offline processing, retirement, timing and persistence. Keep the
independent M1 platform/physical/durability gates visible and active.
