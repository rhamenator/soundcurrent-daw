# Installed Windows batched-verification preview

Checkpoint 339 installs the exact [checkpoint 338 candidate](338-windows-batched-preview.md)
in the independent acceptance clone. Product source remains
`97e9ecdc32cfaddca03e4c8df9829fbdcb68c0aa`, sequence `20261015192606`.
The matching Linux preview is already installed. This checkpoint changes evidence
and documentation, not application binaries.

## Installation and workflow

The actual unsigned installer exits successfully. All **65 payload files** match
the package receipt, Desktop and Start menu shortcuts point to the new executable,
and the per-user uninstall registration exists. The existing Microsoft runtime
remains `v14.51.36247.00`. No compiler or Qt SDK is on the guest's test PATH.

The installed application regenerates a transition using Linux-created history.
Review/Apply, exact Undo/Redo, save/reopen, WAV export and normal close on both
launches pass. Both newly completed spans identify the installed Windows worker.
The host independently computes the mix from the saved clip state and source audio:

- All **18,032 stereo frames at 48 kHz** match exactly, with maximum sample error zero.
- Float peak is **5.5167012214660645**, preserving internal headroom.
- Export SHA-256 is
  `5a88856659abc1fdebb48ebdb452d6948fa9e5bfaffc898e2d7cf3dfeb9bcbac`.
- All **202 original history/media files** in the copied project remain unchanged;
  the original Linux fixture's complete 204-file inventory also verifies.
- All **401 earlier-preview files** remain unchanged. Retirement rechecks the
  new payload and confirms zero owned application processes.

## Original driver failure and timing limit

The initial UI Automation driver queried `compRenderElapsed` immediately after
invoking Save and render, before the new dialog was exposed. It stopped with
`Expected unique control compRenderElapsed, found 0`. The failure, original capture
and live process identity are retained. The application continued rendering.

A separate continuation uses the **same process and same render**, without
restarting either. It observes the completed operation, then performs the remaining
workflow. The application's elapsed label is **13.6 seconds** and remains frozen at
completion. Earlier runs reported 34.9 and 36.9 seconds, but this limited observation
does not establish a general speed guarantee or resolve the independent timing gate.

**The original 30-second observer condition is not passed.** Its driver failed before
timing observations were retained; no independent whole-render stopwatch result is
available. Continuation does not erase that failure. A future observer must tolerate
the dialog's initial absence within the original stopwatch budget, reject duplicate
targets, and retain the original failure if readiness exceeds 30 seconds.

Null control IDs are handled explicitly. Enumeration ignores only
`ElementNotAvailableException`, preserving unique-target and invocation assertions;
the successful continuation skipped zero stale elements. Its newly appearing
controls are awaited within their existing 30-second budgets.

## Evidence, cleanup and next task

The [evidence manifest](../tests/results/M2/2026-10-15-installed-windows-batched/manifest.json)
binds an 85-entry archive, original failure and continuation, installer/retirement
receipts, screenshots, independent verifier and reconstructible project changes.
Archive payloads and reconstructed project hashes verify. It includes no application
binaries or transport credentials.

Only the independent acceptance clone ran, at reduced CPU and I/O priority. It shut
down normally. All VMs are off; temporary receiver, firewall rule and attached media
are removed. Physical audio was not activated. This was an existing acceptance
installation, not a pristine-OS test.

[CI 38797374241](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38797374241)
on documentation head `cae1cb1` remains in progress at this observation. Preserve its
original terminal logs and artifacts before advancing the draft PR. The previously
qualified product-source run is recorded in checkpoint 338. No merge or binary
release is part of this checkpoint.

Next: repair the initial-dialog observation in the acceptance driver and perform one
bounded installed timing check using the unchanged fixture and 30-second condition.
Retain the original outcomes. Interruption/protected-warp findings, sustained hardware
qualification, processing quality, localization and full DAW parity remain open.
