# Owned media provenance, publication and interruption recovery

Date: 2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.

## Implemented workflow

Original GPL C++ modules bind one unambiguous WAVE SourceFile ID20 occurrence to
an admitted inspection, selected reference/replacement and checked audio snapshot.
The immutable provenance retains inspection byte hash/extent, object/property
ordinals and source nodes, exact original token range/bytes, selection policy,
operation UUID, complete WAVE format/frame/peak metadata and source/copy digest.
Original tokens are bounded hex, preserving non-Unicode bytes without interpreting
them as paths. No source root is persisted or reopened by recovery.

Duplicate properties, unsupported sources, ambiguous take ancestry, mismatched
original selection and an implicit replacement for a missing token refuse. The
model binds original values; it does not translate clip timing/gain/fade/rate/pitch
semantics or qualify a native-project adapter. The adapter ID records the existing
RPP inspection-v2 contract, not a new claim about a writer version.

`stageBoundMedia` pre-admits and encodes a planned intent before destination
mutation. It writes `intent.json`, flushes and verifies it before copying any media.
The existing source-byte checks and exclusive generated directory/file rules then
apply. An opaque `stageVerifiedMedia` result without an intent cannot commit.

`commitStagedMedia` checks the exact intent and copied bytes, exclusively renames
`media.partial` to `media.wav`, writes/flushed/readback-verifies `receipt.partial`,
and flushes directory state on Linux. After the last cancellable boundary it
rechecks intent, receipt and media. An exclusive atomic rename to `receipt.json`
is the commit point. Existing names are never replaced, and no Session is changed.
One live commit attempt is allowed; an admission refusal before mutation can retry.

Before publication, failure/cancellation returns no committed result and preserves
inspectable intent/partial data. After publication, cancellation does not undo or
misreport the visible commit. A failed subsequent directory flush returns
`published=true`, `postCommitFlushFailed=true`, file-only durability. Return storage
is prepared before publication. No automatic recursive deletion or resume occurs.

## Native authority and persistence

Linux uses existing pinned directory/file handles, `renameat2(RENAME_NOREPLACE)`,
identity checks on the owned rename source and file/directory/parent `fsync`.
There is no replacement fallback for an unsupported kernel/filesystem. The prior
trusted-destination-writer/mkdir-open assumption remains; this is not a hostile
same-UID filesystem snapshot. See [Linux rename documentation](https://man7.org/linux/man-pages/man2/rename.2.html).

Windows creates renameable owned files with DELETE rights from the beginning.
Bound-operation directory handles permit read/write sharing for the kernel's
relative rename target open while denying delete sharing; intent and media file
handles remain exclusive. Already-loaded ntdll NtSetInformationFile uses native
FileRenameInformation with a simple name, the owned directory handle and
ReplaceIfExists=false. No full foreign path or replacing MoveFileEx is used.
Existing unbound byte staging keeps its prior handle-sharing policy.
See [native rename information](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)
and [NtSetInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntsetinformationfile).
Windows reports file flush only; directory/power-loss equivalence is not inferred.

Local filesystem/ordinary API acknowledgement semantics are the current envelope.
Remote/NFS/SMB storage, lost rename acknowledgements, power loss, hardware lying
about flushes and broader filesystem behavior require separate qualification and
an indeterminate-publication outcome where needed. They are required storage
follow-ups, not silently treated as passing this local transaction.

## Recovery and process boundary

`recoverStagedMedia` opens only an explicitly selected owned parent/operation UUID.
No intent/receipt -> no owned completed evidence; valid planned intent without a
commit marker -> planned partial state. A present corrupt record is refused.
A committed marker must have matching planned intent and operation identity.
Both records are independently parsed with exact schema/keys/types, duplicate/
depth/array/size/count/hex/format/hash checks; unknown extensions need a new schema.
Recovery opens only `UUID/media.wav` through the approved-source capability and
fully re-decodes/hash-checks it against the receipt, including float headroom,
frame/rate/channel/precision/layout/peak agreement. Matching falsified metadata in
both records still refuses when audio differs. Checksums are not signatures, and
recovery cannot establish historical fsync or authenticate a malicious owner.

The codec's value charge is 16 KiB, receipt bank 16 KiB and parser/encode allowance
512 KiB. Stage/commit reuse 64 KiB streaming, crypto/native work grants and exact
handle owners. All allocation, filesystem/decoder work, locks and retirement are
control/I/O only. Decoder/allocator internals, caches, RSS and blocking kernel time
remain outside hard payload guarantees; future GUI consumption must own a bounded
child lifecycle rather than decode on its event thread.

Development-only `sc-media-import-worker` performs a fresh check/commit from an
explicit selected `.scinspect` bundle, source occurrence, root/reference, destination
and UUID, or reopens owned staging in a fresh process. Its report binds actual PID,
hashes and commit/durability state, always `sessionAssetPublished=false`. It retains
published state when a later report step fails. It is not installed or wired into
the desktop. The current CLI fresh-check mode is not approval of an earlier GUI
snapshot: the future GUI route must carry the previously checked provenance/hash
and refuse changes before staging, using the existing checked-report factory.

## Evidence and remaining acceptance

The final local transaction test passes 252 C++ checks and 114 actual child/workflow
checks in Release and ASan/UBSan (leak detection disabled). Earlier scoped six
Release and three sanitized root/WAVE/staging tests also pass; exact cohorts/logs
are separate. MinGW compiles the framework-independent codec/staging core only;
new native MSVC transaction execution is pending for this revision.

| Acceptance | Current evidence | Remaining qualification |
|---|---|---|
| Bound occurrence/selection, original bytes, metadata/hash, codec roundtrip | Actual inspector + independent bounded loader and original C++ checks | More adapters and source/version envelopes |
| Unknown/duplicate/deep/type/extent/hash/precision/authority/policy refusals | Original corruption/admission tests, no partial result/credit leak | OS containment and parser/library breadth |
| Copy and precommit cancellations, collision refusal, changed intent/media | Every relevant trusted boundary; actual files and recovery checks | Broader filesystems and native Windows revision |
| Abrupt interruption before marker | Observed live child, forced termination, terminal wait, new recovery child sees planned state | More crash boundaries and power-loss qualification |
| Visible commit with later stop/flush error | Original outcome checks; Linux actual EIO after publication; marker recovers | Windows storage faults and indeterminate remote acknowledgement |
| Short receipt writes, disk full, precommit flush errors | Linux native-call wrappers + retained partial intent | Native Windows fault and storage qualification |
| Byte/headroom preservation, source absence, fresh process reopen | Independent byte/hash checks and complete decoder agreement | Converted-project Undo/save/reopen/aligned suite renders |
| Original known-writer media | Frozen 7.82 Unicode mono/stereo projects/media stage/reopen unchanged | This test does not execute REAPER or qualify semantic conversion |

Initial test failures are retained: a child wrapper concealed unit stderr, then its
diagnostic showed an incorrect test assumption that property zero was non-media.
The fixture now explicitly contains a sample-rate property, making that ordinal
refusal meaningful. Product binding continues to accept a valid media occurrence
at ordinal zero. No assertion was removed to make a product refusal disappear.

Current logs/hashes and the prior exact final passing PR 69 archives are retained
in `tests/results/X004/2026-10-09-media-provenance-transaction/`. PR 69's final native
152 staging / 61 child checks, Linux 163 / 61 and protected four-context pass belong to
its prior source head, not qualification of this new code. No VM/audio/equalizer,
installed preview, European catalog qualification or public product release changed.

## Next concrete implementation task

Connect a frozen checked-row provenance request to an owned copy child/controller:
explicit destination approval, same-row snapshot/hash/PID binding, admission,
bytes/work banks, cancellation/deadline and terminal retirement, then committed/
planned/failed-durability preview and explicit recovery. Keep source paths separate
from persisted approval. Carry the immutable original inspection/loss report into
owned project state, extend destination gain/fades/rate/pitch/timing rather than
flattening them, then implement conversion preview, one Undo, save/reopen and
independently aligned renders. Refresh and qualify Linux/Windows installed previews.
Multi-file/project atomicity, broader media and all registered native/exchange
adapters, full frozen F/Q/C/N scope and all-Europe localization remain required.

## Final protected platform qualification

PR 70 head 3f5ad5d98ad4ec1b2089cb00ac45d8d4a99a17f5, run 37906089136 passes
all four strict required contexts and merges d5f44fb236c9642038c4ca11f5db21dfc277baa0
without bypass. Linux 88 tests include 252 transaction/114 actual workflow checks.
Native Windows 22 selected core tests include 228 transaction/114 actual workflow
checks; its six desktop tests include 83 media controller/46 media UI/91 inspector
UI/272 localization checks. Native archive digests and exact logs are retained in
the following desktop-copy checkpoint. This qualifies the prior receipt primitive,
not installed operation, semantic conversion, reviewed languages or full parity.
