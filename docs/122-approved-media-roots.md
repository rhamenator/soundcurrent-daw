# X004: approved media folder handles

Date: 2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.
This is a filesystem admission increment toward project imports; it does not
convert a project or certify a file as decodable audio.

## Implemented contract

`sc-approved-media` is original GPL-3.0-only C++20, independent of Qt, the audio
engine and libsndfile. The caller explicitly approves an absolute directory and
receives a move-only facade. Approval binds an open directory object; replacing
its former pathname does not redirect reads. The final selected directory
component must be plain. Selection of the root itself is trusted control input;
this API does not infer consent from a project's strings or select a root.

A canonical UTF-8 relative reference is validated without expansion or path
normalization. It is limited to1024 bytes/64 components/255 bytes per component.
Absolute/drive/UNC/alternate-stream syntax, backslashes, traversal/empty components,
invalid UTF-8/NUL/control characters, reserved device names and nonportable names
require explicit mapping instead. Original source tokens remain untouched in the
existing import report. A successful check does not select one duplicate or
ambiguous SourceFile property or claim foreign filename semantics are converted.

Linux uses a single `openat2` resolution relative to the approved handle, with
beneath/no-symbolic-link/no-magic-link/no-mount-crossing constraints. An O_PATH
pin lets us reject directories/FIFOs/devices before opening for I/O. Only that
pinned plain inode is reopened through the OS's proc-descriptor facility, with
identity/size/timestamp comparison. The foreign pathname is never reopened for
reading. Missing kernel/proc facilities refuse; there is no weaker fallback.
A mounted or linked media folder needs a separately approved actual root.
See the [Linux API reference](https://man7.org/linux/man-pages/man2/openat2.2.html).

Windows uses the loaded OS `ntdll` entry for
[NtOpenFile](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntopenfile),
an approved RootDirectory and a counted Unicode relative name. Documented
[OBJ_DONT_REPARSE](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes)
blocks reparse traversal. The synchronous non-directory handle is checked for
plain disk-file identity and opens with read sharing, denying concurrent writers
and deletion. Lookup leaves case comparison to the OS/directory defaults, without
requesting OBJ_CASE_INSENSITIVE. NTFS supports per-directory case sensitivity;
our owned fixture checks distinct case-only files and intermediate directories.
See [Microsoft's case-sensitivity guide](https://learn.microsoft.com/en-us/windows/wsl/case-sensitivity)
and [object attributes](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes).
No claim is made that the earlier flag caused a wrong-file read in an observed run.
There is no DLL search/load from a foreign path, driver install,
elevated-access request or full-path fallback. Native execution is a separate gate
from MinGW compilation.

Each root/file has one serialized control/I/O caller, including retirement.
Root/file creation, reads, hashes, callbacks and destruction must stay off audio.
A file retains its root object/lease beyond the root facade. The file handle closes
before its slot/lease retires. A trusted per-root open-file quota defaults to32;
this is a simultaneous handle bound, not a project track/asset limit. Linux's
transient path pin makes its maximum native descriptor count root+files+1.
Per-root operations are serialized; callers cannot concurrently share a root/file.

A trusted per-file byte limit is required before reading. Positional reads use
caller-owned buffers of at most64KiB, check extents and verify object/size/modified/
change metadata. Streaming SHA256 reserves64KiB plus conservative crypto workspace;
open-time path work has an8KiB lease. Files are not loaded wholesale into RAM.
Credits describe admitted payload/work, not exact allocator/kernel/RSS usage.
Cancellation polls around chunks and OS operations; blocking OS I/O has no hard
cancellation/deadline guarantee. Linux consistency checks detect observed mutation;
they do not provide a write-proof snapshot against a privileged/hostile filesystem.

## Runnable bounded experiment

The development-only `sc-approved-media-probe` takes explicit `--root`, `--relative`
and `--maximum-bytes` arguments. A successful ASCII JSON record contains exact
relative token, byte count and digest, with `decodedAudio:false`. Failed checks
emit no success stdout. The utility is not added to the current product installer.
For example, with the owned corpus root explicitly selected:

```sh
.cache/build-desktop-release/sc-approved-media-probe \
  --root /home/rich/dev/soundcurrent-daw/tests/fixtures/reaper-7.82/projects \
  --relative media/mono.wav --maximum-bytes 1048576
```

No media access is added to the existing inspection dialog or saved bundle by this
increment. Saved inspection reopening keeps its original self-contained behavior.
The next UI must ask for roots/replacements through explicit controls and do all
resolution/checksum/decode work on its cancellable worker.

## Acceptance evidence

| Workflow | Current evidence | Remaining gate |
|---|---|---|
| Exact plain-file reads/hash, Unicode, empty/binary data, positional reads | Original Linux144/CLI23 and native Windows141/CLI23 checks | Final case-rule revision's native run |
| Root pathname renamed/replaced; file survives facade; native handles retire | Resource and OS descriptor counts on Linux and native Windows | Filesystem breadth |
| Traversal, invalid encoding, reserved names, linked file/intermediate folder, directory/FIFO | Owned refusal fixtures on both platforms; Linux FIFO inspected without I/O | Mount/network/filesystem breadth |
| Byte/file/scratch quotas, missing file, mutation, cancellation | Typed refusals; original owned bytes checked | Hard interrupted-I/O/storage faults and privileged-filesystem behavior |
| Developer argument boundary and binary Unicode file checksum |23 real subprocess checks on Linux and Windows; exact original two corpus-media bytes/hashes | Filesystem breadth |
| Distinct case-only file and intermediate-directory names |159 Linux Release/sanitized checks include owned distinct files and absent mixed-case references | Final native NTFS fixture gate, failing if the fixture cannot be enabled |
| Project source/timeline/audio equivalence | None claimed | WAV decode/copy, reference choices, conversion, independent aligned renders |

Release passes4 selected tests (new root/probe plus session-state/resource-ledger).
ASan/UBSan passes the two new tests, with leak detection disabled. MinGW compiles
both native test/utility executables. The original local selected-run failure was
a missing resource-ledger binary in a partial cache; its log is retained before
building that target. Native Windows tests are added to the existing protected
headless job; no context name/protection requirement is removed.

The first PR66 head1e6eba1bffad3d2ae600842f7a34e8a9ed5f7f7c passes all four
protected checks in run37887344028. Its native headless artifact11596677365 has
verified SHA256 0ac62bf64e8083b054f04dacdbd14f6cb302cce1020f7d68d8365a4427e10b25;
actual folder141/CLI23 checks are retained separately from the earlier Linux144.
An unresolved case-rule review prompted removal of the explicit case-folding flag
and owned case-sensitive fixtures. Those follow-up source hashes and Linux159/23
checks are recorded separately; the initial green run does not qualify changed code.
Only the three owned Windows fixture directories receive a case-sensitivity flag;
no host/VM-wide policy is changed. No new required check or relaxed gate is introduced.

PR65 merged22bb64b after exact23cf5bf passes all four jobs. Its actual native Qt
cohort is controller80/UI90/localization255 checks/34 draft catalogs; the prior
77-check cohort stays separate. Exact artifact metadata/ZIP/log are retained here
alongside the new local source/binary hashes, not relabeled as this media code.
No language is fully qualified. No local VM, host audio route, equalizer repository
or installed preview changed. No public product binary release was uploaded.

## Next concrete slice

Bind independently validated SourceFile objects to explicitly approved roots or
per-reference replacements, with visible missing/ambiguous/unsafe/changed/invalid-
audio statuses. Use the existing libsndfile through this pinned object's bounded
read interface; parse/validate on the worker, avoiding path reopen. Preserve original
reference bytes and portable replacement/loss evidence; keep local approvals out
of shared project state. Add the desktop folder/missing-media workflow on both
platforms, then an opt-in new destination with verified copied assets, transactional
publication/cancellation, Undo/reopen and aligned independent renders. Current
clip/gain/fade/rate/pitch models must grow without silently flattening foreign state.
Refresh exact local installer/source pairs after those concrete workflow gates.
All frozen functional/quality/content/native parity axes remain active/incomplete.
