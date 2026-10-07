# Shared GUI payload admission

X006 checkpoint, 2026-10-07. The desktop's selected-track projections, list indices,
arm decorations and timeline interval/query arrays now reserve declared payload
credit from the same parent as canonical state, Undo and immutable snapshots.
This advances finite resource-admitted project tracks without a product/license
ceiling. It does not establish a fully admitted process or sustained track capacity.

## Ownership and replacement

`sessionForTrack` requires an explicit ledger. The first track borrows the original
immutable Session; selecting another track reserves a conservative full-copy
allowance before copying and rotating that track into the inspector's first slot.
The aliasing shared pointer retains its lease until its last borrower releases it.
Canonical order and IDs stay unchanged. A missing ID returns no projection;
native follow/preparation never silently substitutes the first track.

`SessionListModel` reserves row ordinals, stable-ID maps and conservative node/key
allowances before building a replacement. Matching inventories and filters reuse
existing indices while borrowing new immutable state, including at a full budget.
Changes to track layout, asset rate/layout, selected clip inventory or filters
invalidate the relevant indices. Retained decoration strings/maps are charged;
check-state changes reserve their trial copy before mutation and refuse without
changing a row or calling its edit callback.

`TimelineView` stages row maps, lane clip order and interval trees before replacing
the old arrays. Its maximum single-lane candidate scratch is reserved and allocated
during preparation, then reused for painting and hit testing. Matching track/clip
inventory reuses the existing arrays. Timeline queries do not acquire more ledger
credit. Qt painting, text and control allocations remain outside this scope.

`TimelineEditor` stages its four lists and timeline together. The Studio window
also stages its arm list and selected projection before committing a new display
tuple. Old/new owners overlap during admission; abandoning a stage releases its
credit. Payload arrays/maps are destroyed before their owning credit is returned.
These prepare/commit plans are internal GUI-thread operations and must be committed
once, to their originating component, without an intervening component update.

## Refusal and retry

A rejected selection retains the old selected ID and controls. A rejected published
revision retains the previous complete display and disables editing/preparation
from that stale view. The controller's committed project, history and saved bytes
remain available. Stop, Save, Close and **Edit → Project resources…** remain usable.
The refusal message identifies required/available bytes; users can raise the shared
project budget and retry. **Edit → Retry project display** explicitly retries.

An unchanged refused source/epoch and parent limit/usage fingerprint avoids building
the same rejected candidate on every timer tick. Changed credit or policy permits
a retry. Successful admission publishes the entire staged display and enables its
controls again. The resource dialog reports live shared parent usage, including
these GUI owners, with the existing persistent machine preferences.

Default inspector bindings may borrow the last admitted display tuple in the same
project epoch with a still-present stable selected ID while a command publishes
during poll. Explicit poll tuples require the exact observed source. A refused
display supplies no editing state. Initial recording-monitoring, arming and export
requests synchronize the admitted view before using it; the requested monitoring
value is captured before synchronization can refresh the widget. Preparation
barriers continue to capture the accepted command prefix.

Standalone playback/recording workers receive an explicit projection ledger via
their options; the Studio window supplies the shared parent. Their usual already
rotated projection borrows existing state. Standalone fallback failures follow the
existing typed fault/stop behavior; this is not new native retry/deadline evidence.

## Evidence

`desktop-gui-resources` exercises 512/513-track list and actual-window admission,
full-budget metadata reuse and selection refusal, abandoned stage release,
last-borrower projection release, 10,000 dense clips with repeated paint/hit queries,
atomic display refusal after a committed backend edit, unchanged-refusal suppression,
the actual resource editor, retry, Save and zero owners after window destruction.
`desktop-gui-early-prepare` stops the display timer before Open, then exercises
monitoring/Prepare and three-track arming before the first display. Fake endpoints
do not activate physical audio. Existing 8,192-track sparse viewport, UI, controller
and project tests remain required. Executed counts/logs/hashes are recorded in the
[qualification receipt](../tests/results/M2/2026-10-07-gui-memory-resources.json).

Original failed compile and UI scopes, exact sources/executable hashes/logs and
owned project bytes are preserved. The UI regressions were binding-timing failures:
strict source equality discarded an admitted view while commands published;
monitoring before initial display lost the requested mode; initial arming left the
list inventory stale. Deterministic timer-stopped fixtures reproduce the latter
two and qualify their corrections. Failed compile cases were caller/fixture errors.
The original Save-refusal fixture also preserves a product failure: Save was
disabled because the refused inspector had no editing projection. Save now uses
canonical project existence and retains its IO/close guards. At a full budget it
writes all 513 committed tracks while the retained display still has 512 rows.
The earlier timer-stopped arm fixture forgot to restart its timer for Close;
that separate fixture sequencing failure is preserved and corrected.

## Remaining scope and next task

Charges are conservative declared payload weights, not exact allocator or RSS
measurements. Incoming decoration maps can be allocated by callers before their
retained charge; Qt/control/plan metadata and allocator bucket/capacity behavior
are not fully bounded. General allocation-failure guarantees require more work.
The shared root still excludes prepared old/new/tail graphs, media caches, parser/
encoded IO buffers, expanding edit candidates/commands, waveforms/meters and
other process memory. Qt's signed-int row representation still needs future paging.

Next implement parent leases for prepared graph and media-cache overlap, including
safe retirement, refusal/retry and live/offline equivalence; then coordinate IO and
measure actual allocations/RSS. Larger capture/adoption beyond the current 256-input
implementation, freeze/bounce, prepared scheduling and sustained Linux/Windows
workloads remain required. Cross-compilation does not qualify Windows GUI/audio.

No native audio/VM run, new dependency, schema revision or frozen F/Q/C/N promotion.
The initial audit matched all 24 reviewed equalizer inputs. Before publication,
four review-only DSP files changed as both equalizers lowered their profile-wrapper
post-gain minimum to −60 dB. Their new committed snapshots are retained in
`reuse/reviews/2026-10-07/`; removing that sole constant/validation change reproduces
the previous whole files byte-for-byte. The borrowed coefficient/recurrence subset
and other 20 inputs are unchanged. Refreshed input, snapshot-integrity and isolated
CLI/source-preservation checks pass. All 182 compiled qualification inputs remain
unchanged; original adaptation provenance remains pinned. Original native observation 71
clock/source CPU cause and earlier unresolved causes remain open. X006, the frozen
92 contracts, X004/X005, Windows and European language coverage remain incomplete.

Final qualification: Linux Debug **57/57, 145.16s**; affected
ASan/UBSan/LSan **22/22, 341.79s**; Windows core/media cross-build. No Windows
GUI/native runtime claim. See the receipt for archive bytes, SHA-256 and entries.
