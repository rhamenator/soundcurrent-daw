# ADR088: approved media folder/file capabilities

Status: selected; original Linux/native Windows checked; case-rule follow-up native execution pending.
Date: 2026-10-09.

Foreign media references need user-approved scope before any access. Existing
ForeignSnapshot protects only the selected final file component; MediaReadCache
and ProjectStore assume owned project media. They cannot establish this boundary.

| Route | Functionality/licensing/maintenance | Decision |
|---|---|---|
| Canonical paths then ordinary filesystem/Qt open | Existing dependencies, but mutable pathname validation cannot bind later access | Insufficient for admission |
| Add a filesystem library | A wrapper still needs platform-relative native handles; new dependency/license/update burden | No library selected |
| Original C++ using OS handle resolution and existing crypto | Linux openat2/proc and Windows NtOpenFile/reparse constraints; explicit platform tests and feature refusal | Selected |

No new product runtime dependency. Linux's installed UAPI header carries
GPL-2.0 WITH Linux-syscall-note; we include its declarations and do not vendor kernel
implementation. The kernel documents its
[syscall/UAPI license boundary](https://www.kernel.org/doc/html/latest/process/license-rules.html).
OpenSSL3/Apache2 and Windows OS BCrypt SHA256 remain existing choices; no crypto
algorithm is reimplemented. Existing nlohmann MIT is used only for the developer
probe's bounded report. Python remains a test/development dependency. OS functions
come from the target OS; no OS DLL/header source is copied into the repository.

Costs: Linux requires openat2 and proc descriptors, Windows requires the documented
native entry/flags, and platform/filesystem behavior must be tested independently.
Unsupported facilities refuse rather than falling back. Separate roots/replacements
handle linked/mounted or nonportable foreign references. One serialized caller per
root/file, byte/handle/workspace admission, cooperative cancellation and exact
original-byte preservation remain explicit contracts. Checks are not hard I/O
cancellation, a hostile-filesystem snapshot or an OS sandbox. Checkpoint122 records
acceptance and next UI/decode/copy/conversion work. Full import/parity remains open.
