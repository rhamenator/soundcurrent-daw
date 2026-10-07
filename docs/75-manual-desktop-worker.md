# Manual recording desktop worker and cancellation delivery

Scoped checkpoint, 2026-10-07. Full DAW goal remains incomplete.

## Production ownership

`ManualRecordingController` is a framework-independent desktop adapter with one
serialized worker. It retains an immutable project prefix and prepared
arm/route/playback intent. The worker constructs the endpoint, inventories ports,
routes and activates it, prepares/abandons take slots, submits punch commands,
services disk consumers and joins native/reader/disk owners. The Linux adapter
uses the existing `PipeWireManualRecording`. Windows can build the controller but
still needs a native endpoint with equivalent semantics.

The GUI-facing API admits a 16-command queue and reserves one of 64 reliable
control completion credits before accepting each command. Queue admission,
worker completion and actual audio application are separate. Accepted commands
invalidated by Stop/Close retain terminal Stopped receipts. Generation checks
reject stale intent without replacing or stopping the active graph. Failed
construction/activation is finalized on the worker; a rejected second Prepare
leaves current transport intact.

The controller reserves 64 audio receipt credits across in-flight commands and
its retained inbox. Moving a reply out of the engine does not return application
capacity. Eight result credits cover engine slots plus application-owned groups;
eight completed previews cannot coexist with eight newly prepared slots. Explicit
acknowledgement replenishes admission. Immutable groups carry root/generation and
remain retained until acknowledged, including after Close. They are never
implicitly attached, saved or discarded. Consuming the current snapshot does not
rewrite older snapshots.

Stop/Cancel signals the prepared one-generation atomic interrupt directly through
a short non-RT mutex, independent of the queue. The worker holds that mutex only
for queues/snapshot/handle publication, never endpoint, filesystem or join work.
Requests complete only after joins and receipt collection. Normal Close is
asynchronous; the destructor provides a blocking fallback. Close also completes
the already admitted command prefix. No worker-owned endpoint or mutable engine
reference escapes to the GUI.

Control queues, disk IO, snapshot copies and joins stay outside RT. The callback
still runs only the prepared bridge/graph and existing atomic interruption read;
no callback allocation, lock, clock or logging is added. Canonical history/Save
remains the project owner's responsibility. Taking a result does not rebuild
playback, reset EQ, seek media or advance its model revision.

## Cancel and application handoff

Previously Cancel was sampled only at finalization entry; `cancel()` after
`stop()` was also ignored. The core now observes it between blocking lane
operations, after finalization and at `takeGroup()`. Explicit Cancel reaches
still-owned results after Stop through both the core and native wrapper. Pending
lanes/groups become Canceled while original errors, independently durable
checkpoints and finalized media remain. Canceled groups refuse implicit/partial
adoption.

The acquire observation in `takeGroup()` commits delivery of its next result.
Cancel observed before it affects that result; a request arriving afterward cannot
rewrite the transferred immutable receipt. Previously transferred groups remain
valid. Unserviced retired positive prefixes still drain into bounded retained
media. Running consumers keep their independently durable checkpoint policy;
blocking filesystem operations already executing need not stop immediately.

For the desktop adapter, transfer into its retained result inbox is application
handoff. Cancel affects groups still owned by the engine, not older completed
previews. Explicit dismissal/adoption is separate from stopping current transport.
Widget integration must preserve that distinction in actions and Close/recovery.

## Evidence

See the [dated receipt](../tests/results/M2/2026-10-07-manual-desktop-worker.json).
The regression against unchanged production core failed in the new post-Stop
Cancel workflow. Its combined classification assertion did not print each term;
unlogged original terms remain unknown. Exact original source/executable hashes,
logs and generated project/media were retained before the fix. This deliberate
synthetic development regression is separate from the 48 retained runtime/native
observations. Original 48's CPU spike and sustained performance remain unresolved.

Core tests exercise late method/token Cancel, held real writer hashing/join,
held actual verification and 32 independent captured lanes. Earlier delivered
receipts remain usable; canceled adoption is refused. Every durable sample/origin
is checked and saved originals remain unchanged.

Worker tests use a separate synthetic audio owner and real core readers/writers:

- Eight repeated two-lane groups with one continuously prepared nonflat EQ graph;
  original raw samples, reliable audio replies, grouped history and Save/reopen.
- Replenishment, unused-slot abandonment, stale/duplicate intent, 16 queued
  commands during a held factory, and 64 retained control/audio reply credits.
- Stop-to-Cancel while finalization is held, callback stop independent of the
  held worker, joins before Close and immutable retained receipts after Close.
- Construction/activation failures, worker-only endpoint calls/destruction and
  zero callback RT allocation/free/blocking-lock observations.

Initial Debug39/39 (45.07s); affected ASan/UBSan/LSan3/3 (6.38s), leak detection
enabled. After review, final Debug39/39 (46.29s) and focused controller
ASan/UBSan/LSan1/1 (1.03s) pass; unchanged core retains its earlier sanitizer
qualification. Windows media/controller/test cross-compilation passes. These are
functional/compile checks, not actual-widget, native-controller timing, physical
hardware, sustained or native Windows qualification.

Review found acknowledgements accepted after the last worker sweep could remain
stranded when Closed was published. The deterministic held-Close regression against
the reviewed source records1 control/2 audio/1 group still present after successful
acknowledgement. Its original generated project/source/executable hashes/logs are
retained separately. Final publication consumes that accepted prefix and publishes
Closed under one lock; the same regression records0/0/0. Older snapshots remain
immutable, duplicates are refused and post-Close consumption still works.

## Next implementation

The actual-widget connection is now covered by the next
[manual panel checkpoint](76-manual-recording-panel.md). The results above describe
this earlier worker-only checkpoint; native desktop/controller qualification remains
the next gate after that panel's synthetic functional acceptance.

Wire Prepare, Play, prepare-next-take, Punch In/Out, Stop and Cancel to actual Qt
widgets. Coordinate the canonical-prefix barrier, parameter-following receipts,
route selection, explicit complete/partial group adoption, recovery and Close
without losing previews or resetting the live generation. Add actual-widget
acceptance before presenting this manual workflow as available in the app. Then
qualify the native controller on owned routes and independently on Windows.

Finite prepared horizons, indefinite/loop/seek recording, complete monitor/take/
comp behavior, PDC/plugins, imports, profiles, above-256 project scaling,
all-Europe localization, installers and all 92 frozen contracts remain open.
No F/Q/C/N promotion, dependency, schema or persistent-default change occurs.
