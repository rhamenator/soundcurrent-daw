# ADR 178: Extended Windows paths at owned media I/O boundaries

Date: 2026-10-14. Status: implemented; native runtime confirmation pending.

Original CI retained two nominal worker failures after acquiring the writer lock
and before publishing intent. Their temporary journal paths would exceed 260
characters. Preserve that original evidence and add a targeted native regression;
do not shorten the nested Unicode fixtures to avoid the problem.

Convert conventional absolute paths to extended drive or UNC syntax at explicit
Windows media file API boundaries and the approved-media root open. Resolve
relative paths and lexical separators before adding the prefix. Keep stored
project paths conventional. The conversion is a representation helper, not a
path admission or security authority: existing reparse/descriptor checks,
exclusive creation, no-overwrite publication and lease sharing modes remain.
Metadata checks and journal streams use the same path representation as the
Win32 calls so that they address the same files.

Microsoft documents both extended Unicode paths and the separate registry plus
manifest opt-in for ordinary long paths. Use the explicit representation here
without requiring a registry change during installation.
[Microsoft: maximum path length limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation).

The regression creates a deeply nested Unicode directory, publishes and replaces
journals, reads through owned descriptors and an approved root, rejects audio
overwrite, and rejects a competing lease. On Windows it records an unprefixed
control call under the host's actual policy. Linux execution and a MinGW syntax
check cannot replace native Windows qualification. This change does not certify
all GUI, external plugin, project-store or installer path consumers.
