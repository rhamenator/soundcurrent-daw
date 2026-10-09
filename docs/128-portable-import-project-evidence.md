# Portable original import state and media provenance

Date: 2026-10-09. SC-DAW-BASELINE-2026-10-05 unchanged.

## Implemented contract

Schema **1.8** adds `imports`: stable source IDs, adapter IDs, original source byte
count/hash, an owned inspection bundle descriptor, and original-property-to-owned-
asset receipt descriptors. Schemas 1.0–1.7 migrate to an empty list; older versions
reject the new field. Unknown keys/adapters, duplicate identities/properties,
invalid integer narrowing, missing assets and noncanonical paths refuse.

Files have generated paths under `imports/<source-id>/`. Each descriptor contains
its exact byte extent and SHA-256. The `.scinspect` container preserves the complete
original project bytes, validated originating-worker report, original property
units, unsupported/missing/unverified records and opaque source data. Receipt JSON
preserves original reference bytes, exact property occurrence, selected replacement
policy, original/owned audio digest, decoded format and float headroom. No original
source root, implicit approval, plugin, script or source-suite execution is restored.

`sc-project-import-state` is framework independent. Its factories create exclusive
source directories and new evidence files using the existing qualified native
publication primitive. They never overwrite a previous source identity, attach
tracks, mutate a Session or delete residue. Admission and cancellation precede
mutation. A stopped publication can leave an unreferenced directory/file for later
explicit resolution; its existence does not grant Session adoption. Evidence-file
and project publication remain separate operations, not a multi-file atomic commit.

The pinned bundle overload decodes an admitted container read from an
`ApprovedMediaFile`, without reopening its pathname. Its fingerprint hashes the
EXACT decoded container. Decoding receipts checks their operation/property IDs,
phase and original inspection binding. Owned media is independently decoded and
hashed before a receipt is preserved or an imported project is reopened. A matching
JSON hash alone cannot turn planned, inconsistent or mismatched provenance into
verified media.

## Persistence, ownership and processing boundaries

`ProjectStore` verifies manifest extents/hashes and full typed inspection/receipt/
audio agreement before saving or returning loaded state. A failure leaves the
current project generation unchanged. Previous snapshots preserve the import list.
Moves of the complete owned directory retain references; original media/bundles
can be absent. The immutable admitted `ProjectImportEvidence` retains source/loss/
receipt borrowers until their final owner retires. Existing EQ edits/history,
Session snapshots and playback state preserve the portable descriptors.

Validated sessions index source and asset IDs once. Verification of multiple
archives reuses that checked state rather than repeatedly validating the whole
project or scanning assets for each receipt. This is control/I/O work, including
all file access, decoding, hashing, allocation, locks and retirement; none occurs
in a real-time callback. The engine continues to process only destination Session
semantics, shared between playback and offline rendering.

`ProjectBudget.importEvidenceBytes` is trusted control-side admission (default
256 MiB), separate from the 32 MiB project text/parser/state grants. Evidence keeps
the existing 16 MiB source / 64 MiB protocol / 16 KiB receipt envelopes; the media
policy is at most 8192 MiB per selected asset. Banks and descriptor/vector capacity
are charged before allocation/read. Small factory outputs are canonical descriptor
values: callers account retained state through StateBudget/SessionSnapshots;
immutable decoded evidence separately retains its own leases. Source uniqueness
index work is capped by a64 MiB validation grant before set allocation. These
are payload/work limits, not hard RSS,
CPU, kernel-I/O deadline or sandbox guarantees. Cancellation is cooperative around
native I/O. Very large reports can refuse under the configured work grant.
Media-disabled builds explicitly refuse import-evidence verification.

The existing trusted project/destination namespace assumptions remain: no hostile
same-UID namespace writer, no authenticity/signature or remote-storage lost-ack
guarantee. Storage breadth, power loss, installed platform behavior and OS containment
remain separate gates. No driver, VM, system audio or equalizer checkout changes.

## Acceptance and limits

Actual isolated inspector output feeds the new library, actual media staging/
publication and fresh verification. Tests retain synthetic and two original
REAPER7.82/Linux known-writer cases. Independent Python checks compare original
source bytes, saved archive bytes, owned audio and receipt digests. Core checks cover
save/reopen, backup, EQ history, Unicode/relocation with original media/bundle
removed, schema migrations, corrupt/missing/planned evidence, pinned replacement,
wrong scopes, refusal before mutation, cancellation residue and borrower retirement.
The expanded replacement fixture initially assumed rename leaves pinned metadata
unchanged. Actual ctime refusal contradicted that assumption; the corrected fixture
checks the explicit refusal and untouched success fingerprint. The original failure
and corrected cohorts are retained, with no product guard weakened.
Local Release12 selected tests and ASan/UBSan5 selected tests pass (leak detection
disabled); each of three cases executes153 core checks, plus39 independent Python
checks. Exact native Windows evidence is recorded separately
in `tests/results/X004/2026-10-09-import-project-evidence/`. Native Windows for this
revision is pending protected CI; cross-compilation is not runtime qualification.

This is preserved import state and provenance, **not semantic conversion**. A
`preserved` source property remains in its original units; its report is not
relabeled `converted`. Current Clip still lacks independent source/project timing,
gain, fade, stretch and pitch semantics. Factories are library workflows, not new
desktop import-acceptance buttons. No native compatibility, full parity or refreshed
installed preview is claimed. Existing 34 draft catalogs/738 keys/3135 non-English
draft translations are unchanged; no language becomes fully qualified.

## Next concrete task

Extend the destination clip model and shared live/offline readers for source versus
project timing, gain/fades and rate/pitch. Then deliver explicit conversion preview
with per-property conversion/loss evidence, grouped acceptance with one Undo,
Save/reopen and independently aligned source-suite renders. Keep original/unsupported
state portable throughout. Qualify updated installed Linux/Windows previews with
the four-executable closure. All registered native/exchange adapters, full F/Q/C/N
frozen parity, independent recording/storage gates and all-Europe language coverage
remain required.
