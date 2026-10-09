# Stretch helper preview delivery

2026-10-09. This closes the preview-builder payload gap following
[the desktop controls](136-stretch-supervision.md). It does not qualify an
installed preview or a release. All frozen F/Q/C/N acceptance gates remain open.

## Required payload and provenance

Linux staging requires the exact sibling `sc-stretch-render-worker`, executable
mode 0755, and the unchanged Rubber Band GPL-2.0-or-later license. All eleven
CMake-installed files must match their source/build inputs; missing, extra,
symlinked, stale and mode-only mismatches are refused. Debian dependency scanning
includes the helper. The vendored implementation and license are included in the
matching application source archive.

Windows deployment requires `sc-stretch-render-worker.exe` alongside the other
three helpers. It is qualified as an application binary independently from the
pinned runtime DLL inventory. The installer payload contains the Rubber Band
license and its exact source is in the accompanying application archive.

Both builders require `--stretch-worker-qualification`. The shared gate binds
the current source commit and tree, helper SHA256, native operating system,
protocol v2 and positioned processor identity, observed child and verifier PIDs,
zero integer exits, completed artifact, independently verified audio/sample
hashes, exact output duration, and acceptance counters. Linux evidence cannot
qualify a Windows helper. A successful compile or a worker's completion line
alone cannot satisfy this gate. The receipt is maintainer evidence, not a
cryptographically authenticated statement from an untrusted party.

## Produce an actual receipt

After committing the tracked source, run on the target platform with the exact
helper and independent artifact-test executable that accompany that build:

```sh
python3 tests/stretch_worker_tests.py BUILD/sc-stretch-render-worker BUILD/sc-stretch-artifact-tests --qualification BUILD/stretch-worker-qualification.json
```

Use `.exe` names and the installed Python command on Windows. The test runs owned
synthetic media, independently checks RF64 geometry/samples, then invokes the
shared artifact verifier and live/export/Undo/reopen acceptance workflow. It
records actual PIDs and executable hashes, runs cancellation/resource/boundary
checks, and publishes a new receipt only after success. Existing receipt paths
are refused. Tracked source, commit and executable identities are checked before
and after qualification. Generated build/dependency directories are not tracked
source and may remain untracked. No audio endpoint is opened.

The native Windows core CI job sets `SC_STRETCH_WORKER_QUALIFICATION` for this
same test and preserves its receipt with raw logs. Hosted CI uses its own native
dependencies and checkout commit; that receipt does not qualify a separately
built local installer. Native desktop CI remains an independent application
window gate.

## Acceptance and remaining work

The Linux package fixture verifies eleven staged inputs, five executable modes,
missing/stale helper and license refusals, and extracted Debian modes. Windows
input tests verify all four siblings and 37 invalid render-receipt cases,
including platform, commit/tree, PID/type, exit/type, processor/protocol,
verification, completion, duration and counter refusals.

Next produce and qualify fresh local Linux/Windows preview builds, record native
application and matching helper receipts, and exercise installed workflows and
upgrade/uninstall preservation. Current distribution dependency reviews and
source delivery remain required. No VM, system installation, installer upload or
new release is claimed by this checkpoint.

Current local evidence in
[the final qualification receipt](../tests/results/M2/2026-10-09-stretch-preview-delivery/final-local-qualification.json)
was repeated after the GUI review corrections. All compiled inputs match the
corrected6734a53 Release build, which passed112 tests. Current package fixtures,
actual render/verifier receipt and exact eleven-file CMake staging pass. The
original pre-rebase local receipt remains historical, with its original identity;
the current process receipt is retained separately. Native Windows receipt
production and installer acceptance remain separate pending gates.

Source95cfb7d passed all four CI gates in run37997241623: Linux108, native
Windows core40, native Windows Qt12 and cross-build. The archived Windows process
receipt passes the shared gate and binds its checkout tree to the exact qualified
head. API SHA256, ZIP CRC, raw counters and659 source inputs were independently
checked; retained `native-*` artifacts keep that exact scope.

A local Ubuntu26.04 amd64 preview from95cfb7d was prepared as version
`0.1.0~preview.20261009220811.95cfb7d92a01`. Its extracted payload bytes/modes and
offscreen help/version pass. The3,329,414-byte DEB and719,323,438-byte matching
source archive have retained hashes and packaging commands. Selected source
archive inputs match the commit. Binaries remain local; clean installation,
native audio and release upload are not claimed. Installed Windows delivery
remains required.

The existing32MiB resource test checks only nonzero exit and absence of completion.
Its128-frame source is shorter than the prepared window, so that assertion alone
cannot distinguish allocation failure from span refusal. A separate Linux probe
uses a valid16,384-frame/192kHz/32-channel source:256MiB completes;32MiB emits
`stretch.resource_limit` before creating job files. Vendor initialization occurs
before the ready marker; requiring ready on the low-memory path was an incorrect
initial fixture expectation, and that failure is preserved. Tighten the permanent
fixture with the valid-span positive control and explicit error classification
on Linux and native Windows before treating its counter as sufficient memory
ceiling evidence. This does not change the frozen feature or quality scope.

The permanent fixture now uses that valid16,384-frame source, requires the
specific allocation-error identity with no job directory, and adds the256MiB
completed positive control. Its new counters are173 acceptance checks and34
completed jobs. Local and native execution of this revision must retain their
own scope separately from the preceding172-check/33-job receipts.

The first final Linux hosted run of the strengthened fixture failed at its32MiB
assertion; its raw failure log is retained. A local Debug probe reproduced a
generic initialization error at16/32MiB, an allocation refusal at64MiB and actual
ready at256MiB. A constrained watchdog thread can throw `std::system_error`
before vendor allocation. Resource-unavailable/not-enough-memory conditions now
receive the stable resource identity; other OS errors retain the generic identity.
The positive control gets its own60-second deadline to accommodate Debug builds.
This fixture proves initialization resource refusal, not a particular allocator
stage. New-source local/native qualification must be recorded separately.

[Checkpoint138](138-installed-stretch-preview.md) records actual installed Linux
acceptance of the preceding95cfb7d package. That receipt does not qualify the
subsequent initialization-diagnostic change or a current Windows installer.
