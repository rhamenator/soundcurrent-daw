# ADR 182: Consistent Windows paths for project storage and fixture WAV I/O

Date: 2026-10-14. Status: correction implemented; fresh native confirmation due.

Original native CI at `ff65c8e` passes the long-path media regression and the
version-two child-worker suite, but two deeper history fixtures still fail.
The retained source fixture contains a 142,976-byte unpublished assembly file
whose actual Windows path has 263 UTF-16 units. The next operation hashes it;
the project-store plain-file check still used ordinary Win32 path syntax and
reported missing media. A second fixture reports failure from direct
`sf_wchar_open` output creation in a similarly deep directory.

Use the existing extended-path representation at project-store Win32 calls,
filesystem metadata checks, file streams, locks, publication, cleanup and root
creation. Keep stored project paths conventional and retain existing reparse,
locking and replacement checks. Apply the same representation to the two fixture
WAV helpers. The production media writer already opens through owned descriptors.

Expand the deep Unicode regression to check project-store media hashing,
save/reopen, replacement and previous-project backup, plus an interrupted save
that leaves the committed project intact. Preserve the original nested fixtures
and their paths. Do not shorten the evidence paths or change machine policy.

Linux execution and Windows cross-compiler syntax checks do not prove native
runtime correction. The original archive and complete job log remain retained;
fresh native execution is required. This does not qualify every plugin, external
library or application path consumer. See [ADR 178](178-windows-media-paths.md)
for the underlying Windows representation and official documentation.
