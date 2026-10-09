# Desktop checked-media copying and explicit recovery

Date: 2026-10-09. Baseline SC-DAW-BASELINE-2026-10-05 unchanged.

## User workflow

Open a project inspection, then its media window. Select an unambiguous WAVE row,
choose a media folder or explicit replacement and check it. **Copy checked media…**
selects an existing destination parent; the child creates a fresh UUID folder
containing the copied audio and its original inspection/selection receipts.
Original reference bytes and current project state are unchanged. This does not
create tracks or claim semantic project conversion.

The table separates check status from copy state. Starting a new check clears
that row's prior copy status; previously copied files stay intact. Clearing local
choices releases retained requests/reports, and leaves files intact. Original
paths and checks remain temporary and do not enter saved inspection bundles.

A canceled, crashed, flooded or unverified child can have reached publication.
Its absence of a success report therefore shows **Outcome uncertain**. **Check
last copy outcome** explicitly reopens the chosen destination/UUID, even when no
operation folder was created. **Recover copied media…** can independently select
an existing UUID folder after reopening the application/inspection or relocating
owned copies. Recovery never reopens original source roots, resumes or deletes a
partial operation, or attaches an asset to a Session.

Recovery distinguishes no intent/receipt (no completed copy established), planned
intent without a committed receipt, verified committed media, and corrupt/refused
state. Planned partial data remains available for later explicit resolution.
Verified recovery reports no historical durability evidence. Copy publication with
a later directory-flush failure remains committed and shows unconfirmed directory
durability. Closing the window requests cancellation and returns promptly; the
window waits asynchronously for both check/copy owners to become terminal. Its
closing snapshot retains the uncertain operation/outcome.

## Immutable approval and process boundary

Original GPL `sc-media-copy-protocol` has no Qt or decoder dependency. Its flat
request/reply schemas check exact keys/types, duplicates, depth, limits, UUID,
actual observed child PID, publication/phase/durability consistency and full typed
provenance agreement. A request freezes one same-ledger inspection occurrence,
checked report, selection/reference/native-leaf policy, byte limit, destination
and generated operation UUID. Paths remain transient explicit choices.

`MediaCopyController` admits one immutable request and one QProcess on a separate
low-priority I/O thread. It binds provenance and saves an owned temporary inspection
bundle off the GUI thread, then sends one bounded request over stdin without a
shell. The child reloads that bundle, validates the explicitly selected pinned
source and compares its complete provenance to the frozen expected value BEFORE
any destination mutation. A changed audio snapshot or internally valid different
inspection refuses; it cannot become a newly approved fresh check automatically.
The previous developer CLI remains available, but the desktop uses this checked
request mode exclusively.

Request/reply banks are 128 KiB/32 KiB and stderr 1 KiB. Codec work admission is 4 MiB;
request value charge 64 KiB, receipt value 16 KiB, and parent request/Qt allowance
plus a default 64 MiB child grant precede spawn. Result borrowers keep their grants
across controller/window retirement. Cancellation and the default 60-second child
deadline terminate, then escalate after 200 ms; output overflow kills. Child credit
retirement follows the exact owner's terminal state. Known bounded diagnostics
map to stable translated UI explanations; raw foreign error text is not displayed.

These are declared payload/ownership limits, not hard allocator/RSS/CPU/sandbox
or blocking-kernel deadlines. A large inspection can exceed the child's admitted
work and refuse. Original local filesystem/trusted destination writer assumptions,
remote lost-acknowledgement uncertainty and power-loss/storage qualification remain
as documented in checkpoint 126. Multi-file/project atomicity remains open.

## Packaging, localization and evidence

The actual CMake desktop closure now installs the application plus three sibling
workers: inspector, WAVE checker and checked-copy/recovery worker. Linux staging
checks exact executable bytes and 0755 modes; Windows packaging requires a matching
source, executable and native PID/reported PID/protocol/verified-operation receipt.
Missing/stale/non-executable or unqualified copy workers refuse. Older retained
preview verifiers keep their historical optional-worker contract.

All 34 draft catalogs inventory 738 contextual keys. Existing 3135 non-English draft
translations remain; added strings use English fallback. No language is promoted
to reviewed/fully UI-qualified, and all-Europe coverage remains incomplete.

Local Release and ASan/UBSan acceptance covers 119 actual controller checks and 74
UI checks (screenshot mode adds one image-save check). Actual children cover frozen
source/inspection mismatch, Unicode, original preservation, quotas/single flight,
PID/receipt corruption, pre/post publication termination, deadline, output flood,
lost report, explicit recovery and lifecycle retirement. Existing inspection,
WAVE/transaction/localization and package refusal gates remain separate. Leak
detection is disabled. New native Windows execution is pending protected CI;
cross-compilation is not native runtime qualification.

The first extended UI test timed out because the MiB UI rounded the helper's
byte-limit selector; diagnostics and the corrected test-only selector are retained.
The hook remains absent from the installed worker. Visual review also found a
one-frame stale disabled Copy control: updating the model before deriving controls
repairs the coherent rendered state. Assertions and both earlier cohorts remain
available rather than being waived. Logs, source/binary hashes, screenshot, staged
payload verification and exact prior PR 70 native archives are retained in
`tests/results/X004/2026-10-09-desktop-checked-media-copy/`.

No local VM, audio route, equalizer checkout, system installation or public product
release changes in this increment. New installed/interactive previews still require
separate qualification.

## Next concrete implementation task

Persist immutable source inspection/loss/provenance into owned imported-project
state and extend destination clip timing, gain, fades, rate and pitch semantics
before conversion. Then deliver a conversion preview, grouped acceptance with one
Undo, Save/reopen and independently aligned source-suite renders. Unsupported
semantics must remain visible and preserved, never silently flattened. Refresh
and qualify installed Linux/Windows previews using the expanded worker closure.
Broader media, all registered native/exchange adapters, full F/Q/C/N frozen parity,
all-Europe language qualification and independent recording/storage gates remain
required.
