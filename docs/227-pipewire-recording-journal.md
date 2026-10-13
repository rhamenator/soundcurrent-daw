# PipeWire recording receipts and recoverable common checkpoints

Checkpoint227 extends the bounded receipt codec with a project-owned disk journal.
This is storage and recovery infrastructure; the live capture/render processing
owner, native controller and Qt recording route still need integration.

## Contract

One serialized disk owner writes original floating RF64 audio and packet stamps,
plus separately framed capture/render receipt files. The side manifest stores the
exact raw-journal declaration at the checkpoint, source project/track/asset/run
identities, declared capture/render geometries, generation, counts and SHA-256
prefix hashes. Native POSIX nanoseconds remain intact; the raw packet's legacy
100 ns coordinate must equal floor(native nanoseconds/100). It is not Windows QPC
or evidence of project placement or recording alignment.

Raw acceptance, monitoring copies and native queue/return results remain separate.
Failed native facts can be stored without inventing audio. A finalized paired
prefix says that stored data agrees; it does not prove audible playback, callback
lifecycle completion, all-native success or measured hardware alignment.

## Checkpoint and interruption ordering

The writer commits/flushed raw audio and stamp prefix first, flushes both receipt
files, then atomically replaces the paired manifest. If interruption leaves the
raw checkpoint ahead, inspection uses the older raw declaration embedded in the
last paired manifest. It verifies the exact PCM/stamp and receipt prefixes and
leaves newer bytes untouched. No destructive truncation, automatic project
attachment, timestamp fitting or SDK activation happens during inspection.

An independent native sidecar lease stays held while raw finalization publishes
its media and releases the raw lease. Inspectors acquire both leases and refuse
active writers. Finalization requires receipt producers finished, accepted
receipts drained and the raw bank joined/settled/drained. Empty takes remain
checkpoint evidence, not finalized playable recordings.

Metadata is bounded to 16 KiB, records to 4 KiB, receipt files to 64 GiB each and
inspection uses streaming record/sample pages with resource admission. Unknown
versions/fields, duplicate keys, malformed roles/sequence, geometry/context
mismatches, corrupt committed prefixes and finalized extra receipt bytes refuse.
Digests detect corruption and bind stored prefixes; they are not authentication.
External hostile path replacement remains subject to existing media I/O policy.
Linux flushes file data and directory publication; Windows uses file flush and
write-through replacement, without a directory-flush/power-loss guarantee.

## Qualification and remaining work

Implementation **9b21a5b** (journal e94e0d1; Windows fixture correction) binds810 source/checkout inputs. Four Release suites
pass0.21s: the new durable journal, original raw recording, native receipt codec
and receipt handoffs. The journal exercises eight actual process interruptions
at raw/receipt/checkpoint/final-publication boundaries, plus corruption, extra
finalized tails, active leases, foreign context, small quota, cancellation, moved
Unicode projects and failed native facts. Four current ASan/UBSan suites pass1.49s with leak detection disabled. Four
Windows journal/raw/queue/fixture objects compile warning-free; no current native
Windows link/runtime or SDK/physical route claim is made. The unchanged journal
also passed earlier ASan/UBSan1.59s before the Windows fixture correction.
See [the exact qualification packet](../tests/results/M2/2026-10-12-pipewire-recording-journal/summary.json).

The initial failed-facts fixture incorrectly omitted its return clock, so the
bounded queue refused it. Corrected fixture keeps the recorded return clock while
preserving failed per-port results. A subsequent fixture incorrectly attempted
zero-frame finalization; it now verifies the recoverable checkpoint instead.
Windows compilation additionally caught a process-variable scope collision and
duplicate NOMINMAX definition in the interruption fixture; both are corrected.
All three original failures and fixture sources are retained. Validation was not relaxed.
Fresh Release compilation also retains existing warnings from mix_playback.cpp
and mix_reader.cpp; new journal/fixture targets compile without warnings.

Original PR161/run38307710292 passes all four jobs: Linux148/444.00s,
Windows native75/205.08s, Windows desktop20/106.68s and core cross-build.
Original logs and selected artifact members are retained. These independent
passes do not establish a causal fix for earlier startup/pool/clock-gap failures.

## Next implementation task

Connect this journal to the shared capture/render processing owner using the raw
bank and bounded receipt lanes. Persist route intent, EQ/channel-map state and
joined native terminal/fault receipts with versioned schemas before treating a
live route as complete. Exercise genuine private PipeWire capture + in-process EQ
+ render + journal/recovery together, then integrate controller/Qt workflow.
Maintain explicit user placement until a verified timing anchor exists. Installed
Linux/Windows workflows, physical/quality qualification, unresolved contention,
GUI ownership and all later milestones/European review remain required.
