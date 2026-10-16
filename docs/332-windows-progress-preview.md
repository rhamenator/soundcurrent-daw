# Windows render-progress candidate and required CI evidence job

Checkpoint 332 prepares a local unsigned Windows x64 candidate from
`b5dc2568dbf0e1e623dbaa7d42dcd38fd1eed3ab`, matching the
[installed Linux preview](331-installed-progress-preview.md). Windows installer
acceptance and the installed editing workflow remain pending.

## Native deployment qualification

The original hosted deployment artifact is retained with its published size,
SHA-256 and ZIP CRC verified. All **1,802 native build inputs** and **15 deployment
files** match the frozen source and receipt. An independent development clone
rechecks those hashes and the already installed, reviewed Microsoft runtime.
No runtime reinstall was needed.

Actual native inspection, WAV validation and checked-copy helper checks pass.
Standard stretch qualification passes **386 checks**, including the separate
109,608-check artifact verifier. Protected-warp qualification observes 33 worker
processes, 21 completed renders and **519 checks**. Complete subprocess stdout
and stderr are redirected by the Python parent, avoiding the earlier PowerShell
native-stderr truncation. This successful run does not explain or erase the
[checkpoint-324 initial failure](324-windows-portable-candidate.md).

The deployed main application opens in its native Windows style and closes
normally: PID 8412, exit zero, 2.986 seconds, no forced retirement. Its SHA-256 is
`3d8f067018db4e4a22edd0fbe68d97af1bdafb5eb4101e6ac67f660e3d88c891`.
The comp worker is
`b7835072649d3e19ddca2641c7abe2527bca63c8b428f866abd26f19ba022b56`.
These checks activate no audio endpoint. The owned scheduled task is removed,
all owned deployment processes are gone, and the clone shuts down before host
packaging. All VMs are off; original VMs and the pristine template were not used.

A script-file invocation was initially refused by the existing execution policy.
The retained command-only invocation succeeds without changing that policy.
Console entry initially used the original VM credential, then an entry made while
the lock screen was returning. Login succeeds with the development clone's own
saved credential entered at the observed password field. No credential is retained
in repository artifacts. These setup failures are separate from product tests.

## Local installer and corresponding source

Directory: `.cache/p332-windows-package-20261015172721/`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `SoundCurrent-DAW-20261015172721-b5dc2568dbf0-x64-setup.exe` | 30,444,746 | `2c57ba8cb020ab4753146617dea57f62b7d47e7793724a78bf2a239ae9ff5935` |
| `soundcurrent-daw-20261015172721-b5dc2568dbf0-source.tar.gz` | 2,499,971,571 | `463a8f4e4677cfb2cc6590c5c2c8eded4f803d576bb580e0332974a64c806fb2` |

Full source verification covers all 8,079 tracked entries: 1,802 exact native
build inputs, 6,218 raw Git blobs and 59 Git-filtered blobs. Membership, byte
count, SHA-256 and gzip CRC pass. Dependency sources and notices accompany the
package. Historical evidence accounts for most of the source archive's size;
it is not required for runtime installation. Packaging and verification run
serially at low CPU/I/O priority. The receipt retains
`cleanInstallQualified=false` and `releaseUploaded=false`. The installed Windows
preview has not been replaced.

## Original CI failure and scheduling correction

Original [run 38777492388](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38777492388)
finishes cancelled. All Windows jobs and four Linux shards pass. Linux shard 4
passes all 45 assigned CTests in **691.42 seconds**, after a **934.61-second**
build. It then reaches the job's 30-minute limit during retained-evidence checks;
the required Linux aggregate fails. Passing CTests do not override that outcome.
All 14 original job logs and all 12 original artifact archives are retained.
Archive sizes, published SHA-256 values and CRCs pass; original archives total
1,989,822,953 bytes and remain local.

The 16 archived-evidence steps previously repeated after every shard now run once
in a separate required **Retained workflow evidence (no native replay)** job.
All moved command strings, historical fetches and their order are unchanged.
The synthetic configure/build/test steps, five-way CTest partition, individual
test deadlines and 30-minute job limits remain unchanged. The original required
Linux check now depends on both jobs and rejects failure, cancellation or
skipping from either. All 16 aggregate outcome combinations are checked locally.
Fresh hosted execution of this scheduling change is still required.

Full DAW functional/quality parity, installation coverage, hardware validation,
content and all-Europe localization remain incomplete. Next qualify this exact
installer and the installed editing workflow on the independent acceptance clone,
preserving rollback and the older Windows findings.

The complete moved bank passes locally: **48 Python invocations in 125.43 seconds**
with no root build directory. Historical commit objects were checked locally in
place of redundant network fetches; the hosted job retains every original fetch.
This validates the independent command bank, not the future hosted outcome.

Evidence: `tests/results/M2/2026-10-15-windows-progress-preview/acceptance.zip`,
172 entries, 955,100 bytes, SHA-256
`50c0e4ce4943bad68a2eb3376d8c59440b7cc1697c176971895e1220238c189c`.
ZIP CRC and all payload hashes pass. The archive contains native observations,
qualification scripts, package/source receipts, local command-bank logs, original
CI logs/APIs and the aggregate/command-equivalence review. Large CI archives,
application binaries and installers remain local.
