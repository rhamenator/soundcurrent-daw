# Windows portability preview candidate

Checkpoint 324 prepares a local Windows x64 installer from `61bcaa3`, sequence
`20261015144332`, matching the [installed Linux candidate](323-installed-portable-comp.md).
It is an unsigned development preview, not a public release. Installation and
editing with this particular Windows candidate remain unqualified. The Windows
acceptance clone still has the earlier `2580164` preview.

## Exact native deployment

The independent development clone verifies all **1,794 native source inputs** and
**15 deployment files** against the original hosted build receipts. Its existing
Microsoft runtime is `14.51.36247.0`; the reviewed installer signature and hash
match, and no runtime reinstall was needed. A setup check initially compared the
registry's trailing `.00` as text and stopped. Numeric version comparison fixes
that harness issue; the original failure is retained. An early qualification
invocation also stopped before tests because setup was still verifying inputs.

Actual native import inspection, WAV validation and checked-copy helpers pass.
Standard stretch qualification passes **386 checks**, including a separate
**109,608-check** artifact verification. The protected-warp sequence has an
**unresolved initial failure**: PowerShell's terminating native-stderr handling
retained only the traceback heading. A subsequent diagnostic invocation, with
unchanged test limits and complete output capture, passes **21 renders and 519
checks**, including 18 full PCM comparisons. This success does not explain or
invalidate the first failure. Preserve both observations; investigate using a
wrapper that captures subprocess stdout/stderr without truncating exceptions.

The actual deployed main opens in an interactive Windows desktop, default native
style, and closes normally in 2.606 seconds: PID 5260, exit zero, no forced
termination. Its SHA-256 is
`f7ad35c4e0c61d0d71b11cf04f945176fb6440783da5b0ab31b7e9b75bf63f18`.
No physical audio was activated. The owned scheduled task was removed, deployed
processes retired, and the clone shut down before host packaging. Other VMs,
originals, templates, equalizer repositories and existing previews were untouched.

## Local package and corresponding source

Directory: `.cache/p324-windows-package-20261015144332/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `SoundCurrent-DAW-20261015144332-61bcaa3439ac-x64-setup.exe` | 30,448,198 | `6861299183b80622b9c82e78b8317cd43d32f41ace9d51c276fb8e0d6406c682` |
| `soundcurrent-daw-20261015144332-61bcaa3439ac-source.tar.gz` | 2,490,283,749 | `5c272f692620733537fff962e6b486a85f97fe71c4916f449f4ddf9eb14a5dff` |

The package includes notices and separately supplied dependency sources. Its
source archive is verified in full: **8,057 entries**, including all **1,794
exact native build inputs**, 6,204 raw Git blobs and 59 Git-filtered files. Archive
membership, byte count, SHA-256 and gzip CRC pass. Historical test evidence accounts
for most of the source archive's size; it is not needed to run the installer.
Packaging and archive verification ran serially at low CPU and I/O priority with
all VMs off. The receipt keeps `cleanInstallQualified=false` and
`releaseUploaded=false`. The unresolved protected-warp observation remains an
additional qualification gap beyond what the packaging script checks.

## Reverse-portability preparation

A separate copy of the 204-file project produced by the installed Linux workflow
now has two fresh second-generation requests targeting the Windows worker hash.
One request uses protected clocked processing; the other uses original uniform
context. Both reference the actual Linux assembly
`077d4102-d5bc-466b-8c51-3144e7343698` and its saved history, including two
`address-space` completion receipts. All original 204 files remain unchanged.
Preparation passes 29 fixture checks and releases its reserved memory.

An initial fixture invocation used the wrong mode for the uniform side and
requested an 8,000-frame clip from a 4,032-frame asset. Validation refused it
with `Clip exceeds source extent`. A fresh copy prepared with original-context
mode succeeds. Both attempts are retained; no application validation was relaxed.
These are prepared requests, **not Windows execution or installed UI evidence**.
The next acceptance must exercise regeneration and Apply/Undo/Redo/save/reopen/
export through the installed Windows application; playback of existing PCM alone
would not establish history portability.

## Original CI and catalog correction

The original [run 38757530778](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38757530778)
at `d81edf3` failed both translation-catalog checks in Linux shard 0 because the
export destination accessibility edit changed a UI source hash. Running the real
Linguist update preserves every catalog and translation, changing only that
source hash in `localization/source-inventory.json`. Both catalog checks and
`desktop-export-ui` pass locally: **3/3 in 6.62 seconds**. This source correction
is newer than both prepared packages. It does not imply translation review or
complete European language coverage.

The original run completed with 12 successful jobs; Linux shard 0 and its required
aggregate failed. All Windows jobs passed. Original logs and artifact archives
are retained with size, SHA-256 and CRC verification. No failed run is replaced
by an unchanged rerun. Full DAW, processing quality,
localization and hardware parity remain open.

Evidence: [manifest](../tests/results/M2/2026-10-15-windows-portable-candidate/manifest.json)
and [retained observations](../tests/results/M2/2026-10-15-windows-portable-candidate/acceptance.zip).
