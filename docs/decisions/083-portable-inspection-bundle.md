# ADR 083: persist exact source and unverified inspection in a bounded container

Status: accepted for the structural preview, 2026-10-08.

Use an original, framework-independent, uncompressed `.scinspect` container with
an explicit v1 header, fixed little-endian lengths, exact source/protocol bytes
and a protocol SHA-256 footer. Preserve the source hash and all unverified states
inside the report. Retain the admitted encoded bank in the immutable result,
rather than regenerate JSON from GUI rows or reduce unknown source state.

Use the existing single-request I/O controller for save/open. The live inspection
path validates an actual child PID; reopening validates the recorded originating
PID without claiming a new process or authenticated authorship. The loader shares
the strict protocol decoder and never parses foreign grammar, opens referenced
media/plugins or mutates canonical project state. Payload leases remain with
borrowers and retire after their buffers. No additional dependency/license is
introduced; existing BCrypt/OpenSSL and platform APIs supply hashing/publication.

Publish only to new destinations. Linux uses an owned anonymous temporary plus
`linkat` into a pinned directory, preventing replacement; this requires supporting
filesystems and procfs, with no unqualified fallback. Windows keeps an exclusive
new file handle through `FlushFileBuffers` and relative `FileRenameInfo` with
`ReplaceIfExists=FALSE`; failed temporary cleanup uses `FileDispositionInfo` on
the owned handle. No path-based recursive cleanup or unverified overwrite is
performed. Microsoft's [rename contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info),
[handle update API](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)
and the [Linux link contract](https://man7.org/linux/man-pages/man2/link.2.html)
supply the platform rationale; passing native tests is still required.

Alternatives: ZIP adds decompression/archive-path complexity to a byte-preserving
preview; a converted canonical project would falsely imply semantics; a source
path alone fails portability; JSON regeneration loses exact accepted protocol
bytes. A later import IR can be a new version with explicit migration and loss
reports. Unsupported versions are refused today.

Consequences: reopen temporarily owns both container and copied source/protocol;
the shared budget may refuse a large valid file. Checksums are corruption checks,
not signatures. Linux filesystem breadth, actual crashes/storage failure,
Windows native/Qt/installed workflows and hard OS sandbox/deadline qualification
remain open. Windows named partials can survive forced process death or failed
handle cleanup; no complete crash-recovery claim is made. Source-suite version/
corpus, semantic mapping, approved conversion and full X004 remain required.
