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
