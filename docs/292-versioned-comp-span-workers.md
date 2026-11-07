# Versioned comp span workers over saved source history

Checkpoint 292, 2026-10-14. Full DAW goal remains active.

Standalone uniform span jobs now support the explicit sample clocks introduced
in checkpoint 291. Version-two requests bind those clocks to the reconstructed
source recipe. The worker reviews original saved histories before readiness and
before completion, writes RF64 at the declared output rate, and preserves float
headroom. Selections without regenerated sources retain version-one encoding.
See [ADR 177](decisions/177-versioned-comp-span-history.md).

## Acceptance scope

The fixture creates actual first-generation jobs and adopted source histories at
44.1, 48 and 96 kHz. It then runs nine new child processes: two regenerated spans
and one mixed-selection span at each rate. In the mixed selection only the
unchanged sibling is assembled; its history still has to pass review.

For each result, a separate fixture verifies completed files and compares all
samples against in-process recomputation. This establishes worker/protocol
consistency with the shared DSP; it is not an independent sonic reference.
Tests also reject changed clock fields, missing fields, protocol downgrade,
wrong processor identity, altered readiness/completion, canceled review, a tiny
memory grant and an insufficient source extent grant.

Four actual worker failures cover missing historical completion, missing history
on the unchanged sibling, changed history after readiness, and cancellation.
Missing history prevents readiness and job creation. A change after readiness
leaves a partial file without publishing completed audio. Original historical
markers are restored by the fixture; canonical and adopted project bytes remain
unchanged throughout. Joined verification payload credits must return to their
starting value.

## Qualification

[Source-bound qualification](../tests/results/M2/2026-10-14-comp-span-v2/index.json)
at `499f90563c2cb7ed877fe197bef09c06af7ca1c8` passes five suites in 154.13 seconds:

- Nine completed version-two workers and four required failures, with 350,902
  complete PCM sample comparisons against the shared in-process renderer.
- Six principal historical source reviews: 405,432 PCM comparisons, 393 assertions
  and 132 refusal cases. Verification credits return to baseline.
- Twelve layered recipes, six retained old request identities, fifteen direct
  clock-aware renders and thirty-six legacy uniform renders.
- Legacy exported WAV hashes exactly match checkpoint 290. All observed worker
  processes retire without observation timeouts or retirement pending.

The archive retains 2,069 entries and 60,835,054 bytes, including original test
inputs, PCM, failure files, logs and source/binary identities. This qualification
is Linux synthetic evidence; version-two native Windows execution remains due.

## Remaining workflow

Paired regenerated preparation and canonical Apply remain disabled. The next
step is to retain the full historical dependency snapshot through queued Apply,
recheck it at the project owner, and integrate history review into paired
production/adoption without recursive verification. Actual repeated paired
generations, retained assembled takes and end-to-end persistence/export tests
are required before enabling the UI.

Nonuniform/protected DSP, independent processing-quality gates, the existing
small cross-platform PCM differences, native installer qualification and full
European localization remain open with the rest of the frozen suite baseline.
The installed application, audio routing and VMs were not changed.

## Native failure found in the preceding original CI

Original run [38650069106](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38650069106)
compiled successfully. Its Windows desktop and cross-build jobs passed; the
native worker job failed two source-history fixture setups before readiness.
The Linux job was still running at this checkpoint; the PR head is held until
its terminal original evidence is retained.

The [retained failure audit](../tests/results/M2/2026-10-14-comp-span-v2/native-path-failure/audit.json)
records the full native job log, original archive digest/CRC and failure files.
Both jobs created their writer lock but failed before publishing intent. The
random journal filenames would produce paths of 266 and 267 UTF-16 units,
consistent with the traditional Windows path limit. This is a diagnosis from
original artifacts; fresh native execution must confirm the correction.

Commit `d768fff` converts owned media API paths to extended Windows syntax,
including journal publication/replacement, file descriptors, locks, attribute
checks and the approved-root open. Project paths and machine policy are unchanged.
The [focused check](../tests/results/M2/2026-10-14-comp-span-v2/native-path-fix/receipt.json)
passes on Linux; the affected Windows code also passes MinGW syntax compilation.
Its native fixture exercises a Unicode directory longer than 320 native units
and records the host's result for an unprefixed Win32 control call. This does not
qualify every application path consumer or a Windows installation. See
[ADR 178](decisions/178-windows-media-paths.md).
