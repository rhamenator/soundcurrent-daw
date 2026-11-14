# Current Linux preview candidate and original CI retention

Checkpoint 330 prepares an Ubuntu 26.04 amd64 candidate from source
`b5dc2568dbf0e1e623dbaa7d42dcd38fd1eed3ab`. It includes bounded comp source scans,
the export destination's accessibility association, and visible render stages
with elapsed time. The existing installed preview remains unchanged.

## Current-source qualification

The Release build ran serially at low CPU and I/O priority with every VM off.
All 27 selected preview checks pass in **184.48 seconds**. Coverage includes
recording/controller and media validation checks, export, take audition, paired
crossfades, repeated clocked/uniform comp transitions, actual transition creation,
native stretch helpers, package provenance and localization catalogs.

The qualification receipt binds **1,151 source inputs and 28 executables**, with
unchanged hashes before and after testing. The standard stretch helper passes
386 checks. Protected-warp qualification observes 33 actual worker processes,
21 completed renders and 517 checks. The existing refusal cases remain part of
the result. These are Linux process and synthetic workflow observations; physical
audio, native Windows and complete processing quality are not claimed.

## Local artifacts

Version: `0.1.0~preview.20261015172721.b5dc2568dbf0`.
The Debian version comparison confirms it is newer than the installed
`20261015144332.61bcaa3439ac` preview.

Directory: `.cache/p330-package-20261015172721/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `soundcurrent-daw_0.1.0~preview.20261015172721.b5dc2568dbf0_amd64.deb` | 7,101,012 | `b67b35ecaf7f449f26ba79f9118acf300cb0160549ec49ce699e442e0246d0de` |
| `soundcurrent-daw-0.1.0~preview.20261015172721.b5dc2568dbf0-source.tar.gz` | 2,499,972,831 | `46f4094e8a1f47a6759ceb2f8db4ed74d40ff5424cc3389cb5fd69582117f3b5` |

All 12 CMake installation inputs match the qualified source/build, and every
extracted payload byte matches the staged package. Offscreen `--help` and
`--version` startup pass. The full source archive's 8,079 tracked files match the
frozen revision: 8,073 raw Git blobs and six Git-prescribed filtered files.
Complete membership, gzip CRC, byte count and archive digest pass. Historical
test evidence accounts for most of the source archive; it is not required to run
the application.

This is package preparation, not installed-workflow qualification. The receipt
keeps `clean_install_qualified=false` and `release_uploaded=false`. No Applications
entry, existing installation, physical route or user project was changed.

## Original CI and next native run

Original [CI 38773460845](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38773460845)
at `8c2e447` passes all 14 jobs. All 14 original full logs and 12 artifact archives
are retained, with published size/digest and ZIP CRC verification. Only after
that retention was complete was draft PR #194 advanced to `b5dc256`.

The successful Windows interruption test in this run does not explain the older
failure. The observer cleanup change and new UI are newer than that CI source;
[CI 38777492388](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38777492388)
is now running on `b5dc256` and must be assessed independently. Preserve the
earlier interruption, protected-warp and prolonged-render observations.

## Evidence and next task

`tests/results/M2/2026-10-15-linux-preview-candidate/acceptance.zip` contains 1,588
entries, 3,386,815 bytes, SHA-256
`e7704cadd06489fabd322b0c27f404034078076037be25a5b32ee63496b33aa6`.
CRC and every archived payload hash pass. It retains qualification reports,
build/test logs, package/source verification receipts, original CI logs/metadata,
screenshots and inventories of 2,099 generated workflow files. Generated audio,
application binaries, installers and the large original CI ZIPs stay local.

Next: install into a separate per-user version directory, exercise the actual
application's render/review/Apply/Undo/Redo/save/reopen/export path, verify elapsed
feedback and preserve rollback. Advance the normal launcher only after that
candidate's workflow passes. Retain the current CI's original result and prepare
the matching Windows candidate once its qualification permits. Full DAW parity,
quality, hardware, installation coverage and European localization remain open.
