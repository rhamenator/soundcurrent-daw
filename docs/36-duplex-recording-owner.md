# M2d2: production ownership for simultaneous playback and recording

`DuplexRecordingRun` owns the existing shared playback generation, explicitly
mapped raw capture pools, duplex bridge and one disk worker per armed track.
It is framework independent and suitable for a later Windows native adapter.
`PipeWireDuplexRecording` wraps it in one inactive Linux filter, with packed input
ports and the explicit master output layout. Neither component selects hardware,
changes system defaults or prints EQ/profile correction into the raw takes.

This is a native engine/control foundation. The desktop still exposes selected
single-track recording; simultaneous arm selection, grouped take verification
and canonical history handoff are the next implementation task. The frozen
Bitwig/Cubase baseline and all functional/quality/content/native-format gates
remain unchanged.

## Preparation and admission

Preparation validates the session, fresh asset IDs against all occupied project
IDs, unique armed tracks, project/rate/layout/start/callback bounds, explicitly
packed input indices, monitoring modes, input alignment and checkpoint intervals.
Post-EQ monitoring requires a lane in the immutable prepared mix plan. A fresh
recording spec cannot masquerade as recovered media. Raw channels can explicitly
share a selected input; there is no inferred mapping or truncation.

The owner reserves the declared playback payload budget and adds every normalized
capture pool plus binding allowance through the same
`armedCapturePayloadBytes()` calculation as the bridge. The combined maximum is
256 MiB, with at most 256 armed tracks and 256 packed native input planes. **All
capture admission precedes allocation of any capture pool.** The existing mix
constructor separately admits its DSP/read-ahead/reference payload and verifies
source assets before the native filter is published. These conservative payload
reservations do not measure allocator overhead, process RSS, page faults or the
per-track operating-system thread resources.

The root must load as the same project/rate. This identity check runs again
before starting any writer; immutable unsaved model edits remain legitimate.
Preparation creates no recording jobs. Inactive/pre-writer callback entry clears
certified output without advancing playback/capture or inventing an origin.

## Activation, shutdown and error ownership

Both complete input and master output routes must be selected before activation.
Port count, direction, node serial, name and identity are preflighted by the
existing PipeWire adapter. Stale routes, repeated selection, unrouted activation,
repeat activation and changes after activation/stop are rejected. No rate,
quantum, priority or hardware route is forced.

```mermaid
sequenceDiagram
  participant C as Control/preparation owner
  participant N as Native callback owner
  participant D as Per-track disk workers
  C->>C: Admit all pools and immutable mix; verify root/assets
  C->>N: Publish inactive ports; select explicit routes
  C->>D: Construct all writers/jobs at Record
  C->>N: Activate only after all writers started
  N->>N: Admit one clock; copy raw before EQ/matrix
  N->>D: Publish bounded raw slabs
  C->>N: Request terminal state; stop/join callbacks
  C->>C: Finish raw pipes and cancel/join read-ahead
  C->>D: Drain/finalize or cancel; join every worker
  C->>C: Retire disk owners; retain each receipt/error/job
```

Starting writers is attempted once. A failure constructing a later writer stops
and joins earlier workers without activating audio. The original activation
exception is retained separately from empty earlier-take errors; later writers
are not started. A failed constructor may leave its partial on-disk job for
existing passive discovery diagnostics; no valid job receipt or recoverable
checkpoint is fabricated for an initial-journal failure. Existing media is never
deleted as rollback.

Native activation failure joins native callbacks before raw finishing and disk
joins. Normal stop and cancel use the same order. The generic run's stop/cancel
and destructor require its callback owner to have stopped first; when wrapped,
use the native owner's stop/cancel, never call the generic shutdown directly
while the filter is active. Control methods may block and belong on the existing
preparation worker, not the GUI/audio thread.

Every writer is joined even if another one fails. Receipts and disk errors are
retained per lane; a failed writer does not hide other valid takes. Joined disk
owners are destroyed while their job path and final progress remain available.
This releases descriptors/activity leases even for canceled workers, allowing
exclusive inactive inspection/recovery before destroying the native owner.
Cancellation requests reach every writer before any disk join. It preserves
verified checkpoints rather than reporting canceled takes as successful.
Cancel withholds every take receipt even if a worker has already finalized during
native stop/join. Completed media stays available to explicit recovery; its true
finalized journal and earlier terminal winner are preserved. This policy avoids
intermittent automatic take admission based on disk scheduling.

Reader exceptions are retained separately and exposed after join. Raw accepted
prefixes remain independently finalizable even when live monitoring masks the
file signal but its reader fails. The existing duplex first callback record
remains independent of lossy observations; control-originated faults do not
invent callback facts. Distinct journal-level reader termination is still a
versioned compatibility task.

## Qualification

[Evidence manifest](../tests/results/M2/2026-10-06-duplex-recording-owner.json)
records tested source hashes, logs, finite workflows and open gates.

The framework-free owner corpus verifies 32 concurrent raw writers, explicit
input permutation/stereo matrix, shared origin, exact final partial block,
independent raw/output sample calculations, supplied 41/200-frame alignment,
save/reopen, combined-budget rejection and no preparation-time jobs. It also
checks foreign-root activation, failure of the second initial journal, a real
worker write fault, cancellation with preserved-original recovery, file-reader
failure despite live replacement, inactive/active destruction and joined leases.
The existing bridge corpus continues to qualify clock/shape/queue faults,
headroom, monitor EQ and immediate-control receipts.

The opt-in native fixture connects only owned nodes:
32-plane deterministic source → **production duplex owner** → independent stereo
capture sink. One unarmed file lane plays beside 32 mapped armed lanes. Its normal
range is 480,000 frames from project frame 137. Every raw sample and every mixed
output sample is checked against independently generated inputs/file coordinates
and a float64 sparse-matrix sum. The sink refuses missing clocks or buffers;
after declared teardown, unmapped planes qualify as end of stream only with a
valid contiguous clock and all published graph frames already observed. The
final post-join prefix/sample check still refuses a missed in-flight block.
fault runs compare the valid playback prefix and report any additional observed
sink frames separately. Cancellation/write-fault recovery copies preserve
original verified prefixes. Native initial-journal failure is injected at lane
17 to exercise rollback of 17 earlier started writers.

Bounded callback timing is test-only; collection allocates no callback storage,
and sorting/logging follows native join. Host allocation/free/mutex wrappers
describe their direct observation scope, not all opaque dependency operations.
The harness retains child failures and observes owned-only links, defaults and
cleanup. Native tests run serially after build/CTest jobs finish.

Final qualification passed all 25 debug groups (22.12 s), all 25 ASan/UBSan/LSan
groups (65.12 s) and five optimized core groups (1.27 s). Windows headless core
and tests cross-link, with no Windows execution qualification. The five Release
native cases passed with unchanged defaults/owned-only routes/cleanup. At the
observed 48-kHz/1024-frame quantum, the normal owner callback had a 2.591540-ms
p99 and 2.788460-ms maximum elapsed wall time; this finite corpus does not
qualify sustained deadline/load behavior. Raw/mix differences and missing
file frames were zero. The disk-fault case recovered its 8192-frame verified
prefix; cancellation recovered 49152 frames per lane. Lane17 initial-journal
failure joined all 17 earlier started writers without activating audio.

An initial writer-failure fixture failed on a contiguous sink clock after owner
teardown, with equal 12288-frame graph/sink prefixes. Exact original buffer/flag/
capacity facts were not retained, so that suffix failure's precise cause is not
proven. The corrected fixture explicitly coordinates closure and records shape/
capacity/mapping. Its final write-fault case observed a declared unmapped end
after the complete valid prefix. Production clock-fault behavior is unchanged;
the original failure and qualification limit remain in the manifest.

## Required next work and limits

Implement accepted-prefix desktop multi-arm preparation and explicit packed input
maps/master output selection, live monitor/EQ receipts, and grouped independent
take verification/admission with one semantic Undo/Redo command. Preserve current
selection and edits; a failed lane must still offer its recovery and successful
lanes separately. Then qualify duration/load/alignment and additional failure
workflows against the frozen acceptance register.

Native Windows execution/adapter/GUI, physical recording alignment/PDC, M2's
ten-minute synthetic and 30-minute declared native run, process-kill/disk-full
multitrack recovery, normal dependency unload-memory, memory locking and sustained
deadline/load gates remain open. Earlier native completion/sink gaps and the
concurrent UI timeout remain unexplained; these finite passes do not close them.
Auto/tape monitoring, punch/loop/takes/comping/fades, general routing/PDC/VCA and
all later product milestones remain required. X004 imports, X005 monitoring/
print/portable profiles/rights and all-Europe translation/review/UI coverage
remain separate unfinished work.
