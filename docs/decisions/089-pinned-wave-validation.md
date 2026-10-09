# ADR089: existing libsndfile over approved virtual I/O

Status: selected for the initial uncompressed WAVE check; Linux checked, native
Windows pending. Date: 2026-10-09.

| Option | Functionality/license/maintenance/integration | Decision |
|---|---|---|
| Reopen a validated foreign path | Simple existing API; mutable-path authority can change | Does not preserve approved object identity |
| Pass an OS/CRT descriptor to sf_open_fd | Existing library; Windows CRT mismatch is documented | Avoid this cross-runtime boundary |
| Write a new complete audio decoder | Original licensing possible; codec, precision, format, maintenance and quality burden | Do not duplicate the selected library |
| Existing libsndfile1.2.2 SF_VIRTUAL_IO plus original strict preflight | LGPL-2.1-or-later header/runtime already selected; Linux system package and pinned codec-disabled Windows build; typed callback bridge integration | Selected increment |

The [official API](https://libsndfile.github.io/libsndfile/api.html) documents virtual
callbacks, descriptor/CRT limitations, frame reads and the theoretical1024-channel
ceiling. Current API header/source hashes, notices and Windows archive pin remain
unchanged. No new framework/codec, copied implementation, proprietary asset or
equalizer change. Our preflight/control adapter and original fixtures are GPL-3.0-only.
Runtime/transitive/GPL source-delivery obligations still apply to packaging.

Strict RIFF/WAVE preflight is a bounded admission subset, not an entire new codec
or proof every valid file is accepted. It checks extents before invoking a tolerant
decoder and compares declared/decoded metadata/frame counts. See Microsoft's
[WAVEFORMATEXTENSIBLE definition](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible)
for precision/masks/subtypes. Full WAVE metadata, RF64/W64/compressed codecs,
partial valid bits and larger channel providers remain required follow-up work.

Cost/risk: third-party internal allocations and time between callbacks are not hard
bounded by payload credits or read quotas. The future GUI must isolate the check
in a child with admitted output, deadline/cancellation and terminal retirement;
OS memory/CPU containment needs separate qualification. No hard sandbox claim.
The serialized pinned handle and ledger outlive decoder callbacks; exceptions never
escape a C callback, floating headroom survives, and successful final hash/identity
checks are required before committing copied assets. Checkpoint123 records evidence.
