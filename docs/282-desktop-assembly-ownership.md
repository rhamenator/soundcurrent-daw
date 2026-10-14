# Desktop assembly publication and recovery ownership

Checkpoint 282, 2026-10-14. Full frozen professional DAW scope remains active.

## Implemented service

`CompAssemblyController` queues one saved operation on a dedicated Qt control
thread. It loads the independent project reference, paired selection and immutable
request, verifies both references against the saved project, and inspects the
owned job. Rendering starts only for an absent job; complete jobs are reviewed
without republishing. Other states return recovery-required snapshots.

The service shares the canonical owner's memory ledger and samples root, epoch,
open/create/close state, processing metadata and exact references. Checks run
before work, at publication boundaries and before exposing current completion.
Same-root reopening changes ownership even when the project ID is unchanged.
Selection, request and verification results remain immutable snapshots. The
service never changes canonical audio or treats candidate integrity as adoption
authority.

Cancellation and close use the library's cooperative stop token. The control
thread writes a cancellation marker only into an existing job whose original
intent matches the request. It does not create a job merely to cancel it. A
committed pair remains recoverable after late cancellation. Cancel/close do not
begin another complete PCM review. Normal UI lifecycle polls `closed`; fallback
destruction joins the worker. This service is not an isolated process or a hard
deadline/OS memory-limit guarantee. See [ADR 170](decisions/170-desktop-assembly-owner.md).

## Actual acceptance

Source commit `7d0392a2ef57cb4f38f3c014b3b7b1be7a4607ae` passes the source-bound
desktop ownership suite in 23.88 seconds. The retained
[acceptance index](../tests/results/M2/2026-10-14-comp-assembly-controller/index.json)
records the source/library/executable hashes, original process IDs, projects,
nominal/span receipts, assembly intent/completion/cancellation markers and audio.
All selected source and binary hashes remain unchanged before and after testing.

The workflow starts twelve real nominal/span workers, then three Qt controller
fixture processes using the real `ProjectController`. Fifteen cases cover:

- Successful physical and project-after-SRC publication at 44.1/48/96 kHz source
  rates, read-only review and completed-job reuse without republishing.
- Cancellation after file close, asynchronous shutdown, cancellation before a job
  exists, and late cancellation followed by complete-pair recovery.
- Live clip edits, removal of the trusted reference and reopening the same project
  path/ID under a new epoch, each refusing current completion.
- Unsaved references, insufficient joined memory, changed request bytes and a
  mismatched executable digest, all refusing before a job is created.
- An injected fault after the first final file, followed by refusal to resume the
  interrupted job automatically.
- Busy queue refusal, GUI timer progress while the control worker is held at a
  publication barrier, closed-owner refusal and zero remaining child-ledger
  reservations after each controller retires.

All processes exit successfully. Four original assembly jobs commit; three
cancellation markers include one late cancellation on a committed pair. Six other
started jobs retain canceled/interrupted evidence. No actual process kill, power
loss, stuck filesystem, physical audio or main-window gesture is qualified by
these tests. The application window is not yet wired to this service.

The build used one local low-priority worker and emitted no compiler warnings.
Actual Qt `lupdate`/`lrelease` and catalog check pass: 1,038 keys, 34 partial
catalogs, 3,168 draft translations, zero native-reviewed or fully UI-qualified
languages. New service files are included in the source inventory; no translation
completeness claim is added.

## CI and remaining integration

The preceding original CI run 38626732345 at
`54c7276a0f0ab9d3a77635ecf201987c092acf90` is still running at this checkpoint.
Retain its terminal APIs, full logs, ZIPs and PNGs and audit before advancing
PR 194's head. This controller has no independent Windows execution evidence yet;
its native desktop target, test and artifact retention are configured.

Next implement explicit project-rate post-SRC state shared by live playback and
offline rendering, paired atomic adoption/Undo consuming the reserved output IDs,
and real save/reopen/playback/export before wiring main-window Apply. Preserve raw
takes and nominal anchors; do not repurpose the older same-rate handle window.
Nonuniform/protected span production, spectral-spur and mixed-join quality,
broader layouts/channels/rates/groups, installation, European localization and all
remaining frozen M2–M11/F/Q/C/N/X004/X005 requirements remain open.
