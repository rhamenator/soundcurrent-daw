# Retained immutable session resources

X006 checkpoint, 2026-10-07. Large projects now retain a declared ownership charge
for every unique immutable controller Session block until its last borrower
releases it. This is a concrete part of combined memory admission; canonical state,
history, GUI indices/projections, audio graphs and parser/IO work still need a
coordinated envelope and measured allocator/RSS qualification.

## Ownership and admission

`ResourceLedger` is framework independent. Its copyable facade shares accounting;
move-only RAII leases reserve checked byte totals, resize transactionally, and
release after the owned payload is destroyed. Admission and policy reductions
refuse before modifying accounting. Configuration cannot lower the limit below
live reservations. Overflow is a typed `ResourceLimitError`. Surviving leases keep
their accounting alive after the original ledger or controller is destroyed.

`SessionSnapshots` places a Session and its lease in one shared ownership block.
Aliasing `shared_ptr<const Session>` borrowers count that block once. Copies
reserve before constructing their payload, then reconcile copied capacities before
publication. The charge uses `sessionPayloadBytes` and a declared 256-byte storage
allowance. Existing validation allowances are deliberately conservative; this is
not an exact heap or resident-memory measurement.

Controller options set a trusted snapshot budget (default 256 MiB). Projects cannot
set it. `SnapshotLimits` commands carry a positive byte policy and correlation ID;
results are retained separately from unrelated errors. The policy supports larger
admitted projects without adding a track-count ceiling. The runtime command is
implemented; preference persistence and a dedicated combined-resource editor
remain future work. The existing Undo resources dialog displays live snapshot
reservations, unique owners, limit and peak in the current locale.

Open reserves the configured maximum state payload plus the storage allowance
before decoding on the IO worker. A validated loaded Session moves into its leased
block and shrinks the reservation to its declared size. A small snapshot policy
can therefore refuse Open even if the particular file would be small: the trusted
state maximum must fit alongside retained old blocks. Parser and encoded buffers
continue to use their existing separate `ProjectBudget` policies.

## Concrete controller workflows

- Current GUI snapshots, saved revisions and in-flight saves borrow the same
  immutable block. Saves continue to report the captured revision while later
  edits remain dirty. Command barriers also borrow the current block.
- Parameter, route, monitoring and structural edits prepare the mandatory
  immutable publication before committing an unrelated gesture or changing the
  canonical state. Snapshot-budget refusal retains model/revision/history,
  saved bytes and active gesture.
- A gesture borrows its starting snapshot. Cancel restores that already-owned
  publication and remains possible at a full snapshot budget.
- Read-only history transfer previews follow pending-gesture commit semantics.
  Undo/Redo stage their resulting publication before history transfer. An
  active changed gesture becomes the next Undo; committing it clears Redo.
- Recording attachment proposals are leased across IO verification. Final
  attachment stages the current scalar edits plus verified take group before
  canonical adoption. Recoverable media remains owned if this final admission
  refuses. Existing attachment failure fixtures remain required.
- Successful project replacement prepares the next canonical object/history
  before retiring the old canonical owner. Replacement snapshot admission and
  invalid loads retain the existing project.

`ControllerSnapshot::snapshotResources` records publication-time accounting.
`ProjectController::snapshotResources()` reads live accounting, including releases
of old externally held snapshots when no new project command has occurred.

## Threading and remaining work

The ledger mutex, shared ownership allocation and last-owner destruction are
**off audio only**. Existing controller, IO, GUI, export and graph-preparation
borrowers operate off the callback. This is not an RT allocator or an audio graph
retirement queue. Bounded audio callbacks and existing off-callback graph
retirement remain unchanged.

The canonical Session and EditHistory still have independent declared budgets.
GUI selected-track projections and indices allocate their own state; graph
old/new/tail overlap, caches, project encode/decode work, command payloads,
ControllerSnapshot metadata and Qt/allocator overhead are not covered by this
snapshot ledger. Full X006, sustained/native Linux throughput, Windows native/UI
qualification, freeze/bounce, larger capture admission and all frozen parity
contracts remain open. No additional audio device or interface is needed for
these synthetic ownership tests.

## Acceptance

`resource-ledger` exercises transactional admission/reduction, resize/move/release,
checked overflow, unique borrowing, foreign-lease rejection, facade survival,
512-track snapshots, concurrent off-audio accounting and read-only Undo/Redo
previews.

`desktop-snapshot-resources` exercises 512-track Open, retained old readers, edit
refusal, active parameter/structural/Undo refusal, full-budget Cancel, release and
retry, correlated policy reduction/raise, Undo/Redo, Save/reopen, refused Open,
paused in-flight Save and shared command barriers. Generated project bytes are
retained. The real resource dialog's startup qualification checks the configured
byte-exact snapshot policy and locale-formatted usage label.

Final qualification receipts are recorded separately; successful source compilation
does not establish native Windows runtime or full memory admission.

Final [qualification receipt](../tests/results/M2/2026-10-07-retained-session-resources.json):
Linux52/52,139.51s; affected ASan/UBSan/LSan15/15,
314.38s; Windows core/media cross-build. The CRC/all-entry-byte
verified archive retains scoped sources, executable hashes, logs and owned project
bytes. Native Windows/UI, aggregate/RSS and sustained capacity remain unqualified.
