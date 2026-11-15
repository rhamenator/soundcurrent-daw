# Matching Windows candidate for batched comp verification

Checkpoint 338 prepares the Windows installer for frozen source
`97e9ecdc32cfaddca03e4c8df9829fbdcb68c0aa`, matching the
[installed Linux preview](337-installed-batched-preview.md). The version sequence
is `20261015192606`. The existing Windows installation remains at `b5dc256` until
the new installer and installed editing workflow are qualified.

## Verified inputs and native checks

The hosted `windows-preview-build-inputs` artifact is bound to that exact commit.
Its published size/SHA-256 and ZIP integrity pass. All **1,810 native source inputs**
and **15 deployment files** match independently checked source/deployment bytes.
Dependency review, compiler provenance and the reviewed Microsoft runtime remain
required inputs. In the independent development VM, the existing runtime's version
and the redistributable's valid signature, signer and hash match the review.

The following native checks pass with their existing assertions and time limits:

- Inspection, WAV verification, checked original-media copying and recovery:
  3.60 seconds, preserving exact original bytes and floating-point headroom.
- The v4 stretch-worker suite: 86.02 seconds, including 386 workflow checks and
  109,608 verifier checks in its qualification receipt.
- The v5 protected-warp suite: 59.84 seconds, 519 checks, 33 worker processes and
  21 completed renders. This does not establish full processing-quality parity.
- The deployed GUI opens in the real Windows desktop session and closes normally
  with exit code zero, without forced retirement or audio activation. Its executable
  and comp helper hashes match the native deployment.

The first helper dispatch ran before setup finished and stopped on the missing
`setup.json` receipt, before any helper test. The original failure is retained.
Continuation began after setup completed successfully; no product test or deadline
was changed. Retirement confirms no owned deployment processes, removes the owned
scheduled task and shuts down the VM. Originals and the pristine template were
not started or changed.

## Installer and corresponding source

The local unsigned installer is
`SoundCurrent-DAW-20261015192606-97e9ecdc32cf-x64-setup.exe`, **30,453,967 bytes**,
SHA-256 `ef366fbb676ad821137fb679a37bbf88d4ed6976440a176da1eab91faddcead3`.
Packaging preserves the reviewed dependency/runtime contracts and includes the
required notices and corresponding dependency sources.

All **8,101 files** in the separate application source archive verify against the
frozen source: 1,810 exact native build inputs, 6,232 raw Git blobs and 59 declared
Git-filtered files. Membership, archive SHA-256 and gzip CRC pass. The source archive
is 2,513,907,593 bytes, SHA-256
`885daee3a9caf7a3f64dd3661c41e1217d58d4273a18650ca4a2b44847aca7de`.
The installer has not yet been exercised; native deployment startup is a separate
check. No binary release has been uploaded.

## CI, retained evidence and next step

[CI 38792511012](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38792511012)
on the frozen source passes all **15 jobs**. All original job logs and **12 published
artifact archives** are retained and checked against published sizes/digests and ZIP
CRCs before the draft PR advances. The
[qualification archive](../tests/results/M2/2026-10-15-windows-batched-preview/manifest.json)
contains 127 entries with verified payload hashes, including the first dispatch
failure, successful continuation, native screenshot, package receipts and source
verification. All VMs are off; no physical audio route was used.

Next install this exact candidate in the independent acceptance clone, preserving
earlier previews, then repeat Render/Review/Apply/Undo/Redo/save/reopen/export and
measure the complete operation. Keep the original 30-second condition and preserve
any failure. The previous Windows render-latency and interruption/protected-warp
findings, sustained hardware qualification and full-suite goal remain open.
