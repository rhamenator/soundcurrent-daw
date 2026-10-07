# Shared controller memory admission

X006 checkpoint, 2026-10-07. The controller's canonical Session, Undo/Redo and
active gesture payload, unique immutable snapshots and declared edit workspace now
share one trusted parent budget. This advances resource-admitted finite project
tracks; it does not finish combined process memory admission or certify sustained
real-time track capacity.

## Ownership and policies

`ResourceLedger::child` creates a local domain whose reservation must fit both
its own policy and the parent's remaining credit. Parent/child totals, peaks and
owner counts are updated under one off-audio mutex after all checks pass.
Refusal leaves both counters unchanged. Leases keep their root/scope alive after
the facade is destroyed. `configureWith` checks both live totals before changing
either limit. A child may have a larger configured limit than its parent; both
checks still apply. Cross-root/scope credit transfer is refused.

`ResourceLease::transferTo` moves already-admitted credit between matching leases
without new allocation/reservation. It supports split, merge and retirement at a
full parent budget. Payload destruction precedes release of its persistent lease.
The concurrent fixture exercises eight sibling domains sharing one root.

Controller defaults are **1 GiB parent** and **256 MiB retained snapshots**.
`ControllerOptions` supplies trusted startup values; projects cannot set them.
The existing state/parser/Undo policies remain separate constraints within or
alongside this parent. No fixed total-track/license ceiling is introduced.

Canonical payload is charged using its own capacities; it can differ from the
immutable publication's compact copied capacities. Undo retention and an active
gesture are charged using the existing conservative history payload weights.
Snapshots retain one charge per unique block until the last borrower releases it.
The parent includes saved revisions, paused saves, barriers and external readers
through those shared blocks. Live getters report releases without requiring a
new command; publication statistics are observations at that revision.

## Transactional edit work

The controller reserves two current-state payload allowances before staging an
edit/Undo preview. Read-only history preflights report declared operation peaks;
the controller admits any required additional credit before mandatory snapshot
publication, committing an unrelated gesture or canonical/history mutation.
Already-owned canonical/history bytes are subtracted from that peak to avoid
reserving the same ownership twice. Shrinking persistent charges return credits
before persistent growth consumes the operation bank. Reconciliation performs
credit transfers and needs no new root reservation after mutation.

Cancel restores its already-owned starting publication and only returns credits.
An idle Save or barrier borrows current state. Open reserves the trusted maximum
snapshot payload before decoding, then separately reserves the replacement
canonical copy before replacing current model/history. Recording attachment keeps
the existing verified-proposal reuse and intervening-edit merge paths.

Refusal retains canonical values, stable IDs/revision, history, active gesture and
saved project bytes. Existing limits may be raised for a retry. Policy changes
have their own correlated completion receipts and do not dirty a project.

## Desktop workflow

**Edit → Project resources…** exposes existing Undo limits plus editable shared
controller and retained snapshot limits, live parent/snapshot usage, canonical
and history charges, peaks and unique snapshot owners. Fields and Close are guarded
while a correlated request is pending. The form scrolls and sizes to the available
display. Positive edited values use locale-formatted whole MiB; untouched fields
preserve byte-exact startup values. Both memory policies are applied atomically.

Accepted limits are persisted in application `QSettings` for startup. A settings
write failure reports that runtime limits applied but were not saved; retry is
available. Invalid persisted values fall back to defaults. This numeric formatting
fixture is not translated-language/native-speaker qualification.

## Accounting limits and next implementation task

These are conservative **declared payload/work** charges, not allocator or RSS
measurements. The initial candidate allowance covers copies of current state;
expanding trial candidates and queued command payloads can allocate before the
expanded history preflight. Parser/encoded IO buffers, ControllerSnapshot/command
metadata, Qt/allocator overhead, selected-track GUI projections and indices,
waveforms/meters, media caches and prepared old/new/tail graphs are not yet
coordinated in this root. Allocation-failure hardening remains required. Do not
sum these scoped policies and call it a fully admitted process envelope.

The next concrete implementation task is to lease actual selected-track GUI
projections and list/timeline indices from this parent, stage replacements before
retiring old owners, and expose retryable GUI admission errors. Then coordinate
prepared graph/tail/caches/IO overlap and measure real allocation/RSS. Larger
capture/adoption, freeze/bounce, scheduling and sustained Linux/Windows throughput
remain required. The 256 recording-input implementation cap remains a gap.

## Evidence

`resource-ledger` exercises atomic joint policy failure/acceptance, sibling totals,
credit split/merge at full capacity, facade survival and concurrent child/root
admission. `desktop-combined-resources` exercises 512-track root refusal, retained
readers, active parameter/structural/Undo refusal, full-budget Cancel, Save,
513-track structural growth/Undo/Redo, external fixture owners, reopen and
last-borrower release after controller destruction. The external owner is a ledger
fixture, not an implemented GUI cache.

`desktop-memory-resources` invokes the actual desktop resource controls, including
byte-exact startup before worker publication, scroll/display fit, atomic parent/
child refusal, persistence failure/retry and a refused edit followed by raising
limits and retrying. Existing 8192-track viewport and broader regressions remain
required. See the [qualification receipt](../tests/results/M2/2026-10-07-controller-memory-resources.json)
for executed cases, final-source hashes, logs and preserved generated projects.

The original focused failure sources/executable hashes/logs/project bytes are
retained. Two fixture mistakes were corrected: equality of canonical/publication
capacity charges is not an invariant, and the startup fixture must wait for the
memory button to be enabled after a prior asynchronous history request. These
were test-oracle/sequencing errors, not diagnosed product resource failures.

No native audio/VM run, project schema change, new third-party dependency or
frozen-reference F/Q/C/N promotion is introduced. All 24 reviewed equalizer inputs
remain unchanged. Original native observation 71 clock/source CPU cause and earlier
unresolved failure causes remain open. Native Windows/UI, RSS and sustained
capacity remain unqualified; the full goal, X004/X005 and Europe coverage remain.

Final qualification: Linux Debug **55/55, 145.39s**; affected
ASan/UBSan/LSan **18/18, 334.35s**; Windows core/media cross-build.
The CRC/all-entry-byte verified archive contains **13,830,965 bytes / 978 entries**.
Its SHA-256 is `2f4aecdcaf1a5edd98adc1147faa4907315dd81d339d469b7f7ee61b360d87fa`.

Subsequent [GUI payload admission](84-gui-memory-resources.md)/ADR069 leases
selected-track copies, list/decorations and timeline/query arrays under this same
parent. Its separate receipt qualifies staged display refusal/retry and initial
control timing. The exclusions and next task above describe this historical
controller-only checkpoint; graph/cache/IO, metadata/allocator/RSS and sustained
platform capacity remain open after the GUI addition.
