# S7b: desktop snapshot export

Status: **implemented in the desktop preview, 2026-10-06**. This connects the
[S7a shared-engine exporter](23-offline-export.md) to the window. It is one selected
audio track through its current EQ, not a master/bus/stem renderer or completed DAW
parity. SLICE-001's remaining native/platform/filesystem/routing gates stay open.

## Workflow

Open a project, then choose **File → Export WAV…** (`Ctrl+Shift+E`) or the Audio export
button. Select a track, start/end frames, destination, optional EQ tail and maximum
length, and optional RF64. End is exclusive. The saved project export range is the
initial selection; an empty range uses that track's clip extent. Frames use the current
locale's numeric formatting. The selection also shows duration in seconds. Unicode
track names and paths use stable track IDs and native/UTF-8 filesystem conversion.

Pressing Start captures the accepted current project settings, including unsaved
parameter edits. It does not save the project. Later edits still reach playback and
recording monitoring while the private export instance retains its original snapshot.
Raw captured media and the saved project file are unchanged. Default export includes
track EQ, with no equipment/room correction route; X005 monitor/print work remains
separate and incomplete.

The worker checks the destination. If a plain existing file is admissible, a single
asynchronous window-modal prompt shows its path and current size, defaulting to No.
No, prompt close or job cancellation keeps the original. Yes binds approval to that
job and inspected SHA-256. Changed/missing targets fail rather than being overwritten.
User approval is not inferred by choosing an existing name in the file dialog. Parent
directories must already exist; inside the project only `exports/` is allowed. Linked
replacement targets and NUL paths are refused. Content revalidation is not atomic CAS
against hostile concurrent filesystem writers; the owned-media/path contract applies.

Progress and Cancel remain in the **status bar outside the scroll area** while a job
is busy. File → Cancel export is also available. The Audio export section retains
completion/failure details, written peak, preserved over-0-dBFS values, capped tails
and durability/publication warnings. Large jobs use RF64 automatically; float WAV
retains headroom. Neither a canceled unpublished job nor a failed prepublication job
publishes a partial destination. A completed publication wins a cancellation that
arrives after the core's publication point.

## Ownership and limits

`ProjectController` keeps one immutable barrier receipt with session/root/revision at
the exact accepted-command prefix. Opening the dialog obtains a receipt for defaults;
Start obtains a second receipt for the actual job. A later model publication does not
mutate the prefix model. The dialog's local range/track choices remain distinct from
project state. The selected track ID is validated again during core preparation.

`ExportController` owns a low-priority QThread and exactly one admitted job, covering
queue, destination inspection/hash, consent wait and rendering. A second submission
returns Full without replacing the first. GUI operations only configure, submit, poll,
answer and cancel. Model validation, source/destination hashes, decoding, buffer
preparation, DSP, WAV writes and publication run on the export worker. The engine/core
have no Qt dependency; they remain shared with native live processing.

Cancellation/shutdown flags bypass admission. A blocked operating-system file operation
must return before cleanup can finish; the GUI keeps polling and stays responsive.
Progress replaces one latest snapshot at most every 50 ms; there is no unbounded
history/signal queue. The rendering core and session/media contracts provide work,
channel, memory, source-handle and tail bounds. This is not a general graph scheduler
or arbitrary-latency export path yet. Cooperative cancellation cannot interrupt a
stuck kernel syscall, nor does a low thread priority prove deadline performance.

Normal window close cancels export and waits for its job/core cleanup, stops native
playback/recording and finalizes/attaches a pending take, then obtains the dirty-close
barrier. Save/Discard/Cancel remain the canonical project choices. Final window exit
requires all four project/playback/recording/export owners to acknowledge joined
shutdown. Cancel-close retains project edits and leaves the export worker available.
New/Open are blocked while an export configuration/job is active; accepted EQ edits
and explicit Save can continue against the canonical session.

## Qualification and gaps

The controller and actual modal/nonmodal UI fixtures use real owned capture media and
real export writes. They qualify snapshot-prefix capture, later live EQ receipts,
single-flight admission, wrong/stale/duplicate consent, changed targets, replacement,
RIFF/RF64/tails, localized frame parsing, Unicode IDs/names/paths, screen fit and wheel
guards. Fault fixtures cover canceled hashing, a live partial writer, before-publication
cancellation, published-warning success and blocked-close cleanup with dirty Cancel.
The exact shared-engine render hashes are compared to the desktop output.

The opt-in native playback fixture's `export` mode adds a desktop WAV job while a
production PipeWire playback owner renders to an owned sink. The exported static
snapshot prefix is compared with matching native live samples before a later live
EQ edit; the full native signal still follows its applied edit/undo receipts. Routing
observations verify defaults, pre-existing links and cleanup independently.

Debug and sanitizer regression passed all 16 groups; the final two affected UI groups passed after visibility changes. Native debug export compared 26,624 snapshot-prefix samples and 480,000 playback frames with zero difference/missing frames; the sanitizer diagnostic compared 25,600 prefix samples. That native sanitizer run uses `PIPEWIRE_DLCLOSE=false`, leaving normal module-unload memory qualification open. The Windows headless cross-build passed, without Qt or native execution.

See [S7b evidence](../tests/results/SLICE-001/2026-10-06-desktop-export.json) for actual
results. Full Windows desktop/native audio execution, delivered translations/native
reviews, accessibility/HiDPI, physical disk-full/power-loss, completed >4 GiB jobs,
crash-temp discovery, hostile path containment and normal PipeWire unload-memory
qualification remain open. Locale mechanics and translation contexts do not establish
all-Europe language support. Existing source-rate, unused-media, zero-latency/static-EQ
and processor limitations from S7a remain; no frozen parity row is marked complete.

Next: S8 end-to-end native first-slice corpus, reopened/relocated project and export
comparison, explicit routing/raw-versus-monitor checks, and the remaining M1 durability
and platform gates. Follow that with dependable multitrack state/recording/undo/recovery
in M2; keep the full frozen-reference scope intact.
