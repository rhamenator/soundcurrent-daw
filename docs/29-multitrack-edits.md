# M2a: transactional track and clip editing

## Delivered contract

The Qt-free session library now exposes stable-ID `SessionEdit` operations:
insert/remove/rename/reorder audio tracks; insert/remove clips; set non-destructive
timeline/source/length ranges; move clips within or between compatible tracks;
and split a clip at an interior timeline frame. A batch contains 1–64 operations,
validates before publication and either commits its entire result or leaves the
session intact. IDs and insertion anchors are explicit. A split retains the left
clip ID and requires a fresh right ID; the right source offset advances by the
left length. Raw assets/files remain untouched by editing.

Track creation accepts mono, stereo or discrete layouts up to 256 channels and
uses new track/processor/band IDs, empty routing, Off monitoring and flat EQ.
Default EQ bands omit frequencies above Nyquist at low project rates. The session
limits remain 256 tracks, 4,096 assets and 8,192 clips. Existing schema 1.2 already
represents these objects: no format or dependency change is needed.

`EditHistory::structural` records one undo unit per batch. It shares the history
with scalar gestures, routing and monitoring. History retains at most 256 units
and 32 MiB of conservatively counted object/vector/string payload; it retires the
oldest complete units. This accounting is not a hard allocator/RSS bound. Each
change retains only changed track/asset values and changed ID orders, plus export
end when relevant. Structural Undo/Redo validates expected values and order, constructs the
replacement off RT and publishes atomically. Unrelated unchanged objects survive;
conflicting external changes fail without consuming the entry or mutating state.
No-op groups do not clear Redo or advance the controller revision. Session
validation reads the band already being visited rather than repeatedly searching
all tracks by parameter address; public parameter lookup still checks stable IDs. An active
parameter gesture must be committed/canceled before direct structural edits.

`ProjectController::Structural` prevalidates the whole group before committing
an unrelated gesture, then applies to the latest canonical model. Edits continue
while a captured save is blocked; saved content/revision and dirty state keep
their existing exact-prefix rules. Barrier receipts capture the accepted group.
Structural edits never directly replace, reconnect or activate a prepared graph.
The existing transport owners retain their preparation snapshots; multitrack
shared-clock graph integration follows in M2.

## Verified take admission now joins Undo

The recording I/O owner still verifies finalized journal/media/hash evidence
before canonical admission. Completion rechecks identity and applies to the
latest model, preserving edits accepted during verification. That accepted
track/asset/export-extent change now joins the same structural history.

Undo detaches the clip and newly admitted asset metadata and restores the prior
export extent. It leaves the owned raw recording job/files intact for Redo or
explicit recovery review. Redo restores the exact objects/IDs; it does not perform
new disk verification. Later playback/export preparation still verifies media.
There is no media deletion or implicit disk garbage collection. The attachment
receipt counter describes completed admissions, not current asset count.

## Usable developer workflow

`sc-project-tool inspect DIR` prints canonical IDs/state. The following operations
load, validate, edit and transactionally save a project, then print the resulting
JSON. Unicode names/paths use the existing platform conversion. Frames are strict
nonnegative decimal integers. `-` means insert/reorder at the end.

```text
sc-project-tool add-track DIR NAME mono|stereo|discrete:N [BEFORE_UUID|-]
sc-project-tool rename-track DIR TRACK_UUID NAME
sc-project-tool remove-track DIR TRACK_UUID
sc-project-tool move-track DIR TRACK_UUID BEFORE_UUID|-
sc-project-tool add-clip DIR TRACK_UUID ASSET_UUID START SOURCE LENGTH
sc-project-tool remove-clip DIR TRACK_UUID CLIP_UUID
sc-project-tool trim-clip DIR TRACK_UUID CLIP_UUID START SOURCE LENGTH
sc-project-tool move-clip DIR FROM_TRACK TO_TRACK CLIP_UUID START
sc-project-tool split-clip DIR TRACK_UUID CLIP_UUID TIMELINE_FRAME
```

CLI commands save separately; they do not persist an undo stack across processes.
No-op commands do not rewrite the project. Failures preserve existing project
files. Existing `new` and `inspect` remain available. These are developer controls;
desktop track selection, a timeline and editing bindings are the next task. The
current transport UI still operates its prepared first track. This foundation
alone does not establish multitrack playback/recording or M2 parity.

## Frozen-reference acceptance decomposition

These existing source-linked families remain unqualified; this checkpoint only
supplies the following concrete subsets of their full workflows:

| Family | Implemented/tested subset | Remaining acceptance |
|---|---|---|
| [P008 grouped edits/phase coherence](01-parity-matrix.md#recording) | Atomic eight-track coordinate edits preserve start/source offsets; one-unit Undo | Group-selection UI, takes/comping, linked warp markers, rendered/native phase and reference comparisons |
| [P009 fades/crossfades](01-parity-matrix.md#recording) | Interior split and non-destructive trim preserve independent source coordinates, mixed Undo | Fade/crossfade model/curves/UI, render tests and reference options/defaults |
| [P086 Undo/Redo](01-parity-matrix.md#reliability) | Scalar/route/monitor/group/split/take admission mixed history, conflicts and raw-file preservation | Presets, all later editor domains, history browser, durable-history policy and full reference comparison |

No F/Q/C/N axis is promoted from these subsets. Track insertion/reorder and clip
identity tests are infrastructure for P001/M2, not its 32-track capture evidence.

## Evidence and remaining work

[Evidence](../tests/results/M2/2026-10-06-multitrack-edits.json) separates:
transactional geometry/state tests, independent source-coordinate split oracle,
eight-track grouped-offset checks, mixed history and conflicts, count/byte limits,
controller save/gesture/barrier/admission integration, developer CLI persistence,
and platform compilation. Coordinate checks are not physical linked-microphone,
DSP quality or timing qualification. Neither broad M2 nor a frozen parity family
is marked complete by these tests.

Next: desktop stable track/clip selection and a timeline that binds these commands,
then a bounded shared-clock live/offline multitrack graph and simultaneous overdub.
Punch/loop recording, takes/comping, fades, grouped selections, missing-media
relinking, autosave/journaling/backups and all remaining M2 workflows stay required.
Native Windows/physical/load/durability/normal module-unload, X004/X005 routing and
rights, and all-Europe translation/review/UI gates remain open.
