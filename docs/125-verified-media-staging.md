# Verified media staging foundation

Date: 2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.

## Implemented workflow and ownership

Original GPL C++ `sc-media-staging` takes a freshly pinned approved source, its
checked byte count/SHA-256, a trusted maximum byte count, an explicit absolute
destination parent and an operation UUID. It creates a new exclusive UUID
subdirectory and `media.partial`. Source names never become destination names.
The same source ledger admits retained ownership plus a 64 KiB transfer bank,
8 KiB crypto allowance and 16 KiB native-path/work allowance before any write.
The existing source digest has its separately admitted bank. Provider/allocator
internals, OS caches and RSS are not hard bounded by these payload charges.

The source checksum is reverified before destination mutation, during transfer
and after destination verification. Source reads retain the existing metadata/
identity checks. The copy is flushed, then independently hashed through its open
read/write handle; extent must agree before and after readback. Short native
reads/writes loop until exact completion; zero progress refuses. Work is serialized
control/I/O, with no Qt, audio callback, locks on audio or whole-file allocation.

A successful move-only result owns the parent, newly created directory and asset
handles plus retained admission until retirement. It remains valid after source
and root facades retire. Destruction closes handles and releases credit; it never
recursively deletes staging data. Cancellation is cooperative around blocking
native I/O. A failure returns no verified result and leaves any partial data
inspectable. Existing projects and source bytes are untouched.

The development-only `sc-media-stage-worker` invokes this actual implementation in
a separate process. Its bounded flat v1 output records operation/path/count/hash,
platform durability and actual PID, explicitly `publishedAsset=false` and
`decodedAudio=false`. It accepts portable approved references; manual nonportable
source leaves are supported by the core approved-file API. The worker is not yet
installed or consumed by the desktop; this does not refresh preview installers.

## Destination authority and durability

Linux opens the explicit parent with `O_DIRECTORY|O_NOFOLLOW`, exclusively creates
its UUID child using `mkdirat(0700)`, then opens that plain child with `openat` and
checks owner/mode. File creation is `O_CREAT|O_EXCL|O_NOFOLLOW(0600)`, with plain
single-link file inspection. Subsequent operations use those handles, including
file, child-directory and parent-directory `fsync`. The result reports
`FileAndDirectoriesFlushed` only after all succeed.

The parent and namespace writers must be trusted. Linux mkdir/open is not an
atomic capability for the newly created inode against malicious same-UID parent
writers. Neither a user-selected pathname nor an unguessable name establishes a
hostile-filesystem snapshot. A later full transaction must preserve this explicit
assumption or supply a stronger qualified backend; there is no pathname fallback
for foreign source access.

Windows opens/inspects a final plain parent with CreateFileW, then invokes the
already-loaded ntdll NtCreateFile relative to its handle. FILE_CREATE refuses
existing names; OBJ_DONT_REPARSE and directory/non-directory constraints apply.
Directory creation omits FILE_OPEN_REPARSE_POINT because Microsoft's documented
compatible directory options omit it. Owned directory/file handles deny sharing.
File readback uses the same open handle. FlushFileBuffers provides the reported
`FileFlushed` state; Windows directory/power-loss durability is not inferred.
Directory security inherits the parent/default ACL and is not a new OS sandbox.
See [NtCreateFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntcreatefile),
[CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).

## Acceptance and scope

Linux Release: five selected tests pass, including 163 staging checks, 61 actual
worker checks, unchanged 168 approved-root and 20,508 WAVE checks plus 64 WAVE CLI
checks. Three relevant ASan/UBSan tests pass with leak detection disabled. MinGW
compiles the new core/worker/tests; this is not native execution. New native MSVC
checks are added to the existing protected context and remain pending for this
revision. Exact logs, source hashes and the earlier final PR68 receipts are in
`tests/results/X004/2026-10-09-verified-media-staging/`.

| Workflow | Evidence | Limits / remaining gates |
|---|---|---|
| Fresh snapshot, Unicode source, generated destination, byte-identical copy | Actual core/child and independent Python byte/hash oracle | More filesystems and broader sources |
| Float WAVE with 2.5× headroom | Actual source and staged pinned decoder reports agree | Quality/render equivalence of converted tracks |
| Collision, stale checksum/size, missing/traversal/reference/path refusal | No destination mutation; sentinel/source unchanged | Durable occurrence/provenance binding |
| Every boundary canceled or throws; mid-transfer cancellation | No verified result, source unchanged, credits/handles retire | Hard OS deadlines and interrupted-job recovery |
| Native short writes, ENOSPC, zero progress and EIO flush | Linux linker-wrapped OS calls, partial data retained | Windows actual storage failures and filesystem breadth |
| Tampered readback; renamed opened parent | Linux owned fault/namespace fixtures | Hostile destination namespaces are excluded |
| Result survives source/root retirement and moves safely | Retained charge + exact native handle counts | Multi-file transaction admission/retirement |

The first extended decoder comparison failed because its test requested `sha256`
instead of the existing WAVE protocol's `sourceSha256`. The exact failed log is
retained. Correcting the assertion preserves comparison against the actual
protocol; it is not a product failure or relaxed oracle.

## Required next task

Implement a versioned owned staging/provenance receipt binding original inspection
schema/digest, source object/property occurrence, original reference versus explicit
replacement, checked format metadata and verified staged hash. Add exclusive
commit/publication and interruption recovery rules before any Session asset is
published. A `media.partial` file alone must never be considered complete.

Then add desktop destination selection with owned child deadline/cancellation,
explicit conversion/loss preview, and destination timing/gain/fade/rate/pitch state.
Qualify one Undo operation, save/reopen and independently aligned source/destination
renders, then refresh and test both installed previews. Current copies do not
convert projects, carry a durable provenance receipt, implement cleanup/recovery,
or claim native-project compatibility. Other registered native/exchange families,
broader formats, Windows/audio/alignment, all-Europe localization and full frozen
functional/quality/content/native parity remain required and incomplete.
