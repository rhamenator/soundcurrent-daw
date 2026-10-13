# Release copied raw slots before paired durability work

Checkpoint 240 improves the recording disk owner's service ordering and measures
full raw checkpoint duration. Full frozen-reference parity remains active.

## Service ordering

Previously a capture append could perform due paired durability publication while
its copied raw bank view remained held. The disk owner now requests deferred paired
checkpointing, writes and hashes raw samples/receipt metadata, releases the raw view,
and calls `checkpointIfDue`. The original append API still checkpoints immediately
by default. Render append, checkpoint interval, raw/receipt flushes, hashes, atomic
journal publication and finalization semantics remain unchanged.

This frees one raw slot during due paired publication. It does not eliminate long
storage stalls or establish that earlier QueueFull failures are resolved. Automatic
raw checkpoints during raw append still may run before the view is released; that
remaining service risk is explicit. No audio callback, queue size, clock confidence,
deadline, sample-loss or completion gate is relaxed.

A disk/control `BeforeCheckpoint` hook is appended after the existing raw boundary
IDs; it brackets the entire raw flush/journal publication through `AfterJournal`.
The test measurement distinguishes that completion from final media publication.
It is not invoked by realtime callbacks and changes no project schema.

## Acceptance and recovery

Regression checks prove due paired publication occurs after raw release, no-due
calls do not repeat publication, immediate render semantics remain, and verified
raw/receipt prefixes reopen exactly. An injected deferred publication failure
retains the older verified common prefix and refuses further use of the failed
writer. Existing eight actual process interruption cases remain in the journal suite.
The initial regression incorrectly treated final media `AfterJournal` as checkpoint
completion; its original log is retained and the measurement distinction corrected.

Current source 9f40338 binds 839 selected inputs. Three Release suites pass in
7.38 s: owner 5.09, durable journal 0.07, controller 2.22. Genuine private PipeWire
1.6.2 recording/admission/Save/Reopen/native playback passes: 99,072 bit-checked raw
frames, 8,192 monitoring/sink frames, 387 capture/408 render receipts, explicit
placement 257 and eight evidence files. All 8,192 live samples match offline EQ
exactly, with zero missing frames and callback allocation/free/lock hits; all owners
and daemon join. No physical/default/volume/user playback/VM/equalizer changes.
Four changed Windows disk/owner/journal-test objects compile without warnings.
This is compile evidence; current native runtime and sanitizers remain open.

## Original measured CI

PR172 original run 38332233927 at cc0d1d5 is terminal and retained. Linux154/455.15 s
and cross-build pass; Windows native79/81/251.19 s and desktop19/20/62.68 s fail.
The measured desktop failure has no sub-millisecond dispatch bursts, raw QueueFull,
no startup, raw write3.65 ms and paired publication41.52 ms excluding raw flush.
Native failures include actual dispatch bursts and larger write/publication spans.
These observations support investigation of storage service pressure; they do not
prove the complete original latency cause. Failed original bytes remain local,
public source packets contain only logs, text, metadata and hashes.

PR173 original native81/184.39 s and cross pass; Linux/desktop remain live at the
current observation. A later pass does not causally resolve earlier failures.
Next qualify current Windows full raw-checkpoint timing/service behavior, current
sanitizers and remaining playback/recovery/alignment/hotplug workflows.
