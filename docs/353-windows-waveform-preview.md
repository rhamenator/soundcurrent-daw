# Matching Windows waveform preview and resource-test correction

Checkpoint 353 prepares the Windows candidate from the same frozen product source
as the installed Linux waveform preview: `f2f9f7c8916e34582db9f289f62bda2019d1c330`.
It does not promote the Windows installation or qualify the newer dense-overview
implementation from checkpoint 352. Full-suite parity remains incomplete.

## Original CI result and repair

Run `38818238344` finished with a failed Linux GUI resource test and failed Linux
aggregate. Windows desktop testing and deployment collection passed. All 15 full
job logs and all 12 original artifact archives (1,990,193,499 bytes) were retained
and verified before advancing the pull request. A successful Windows job does
not turn this overall run green.

The GUI test took a memory baseline before asynchronous waveform preparation.
After raising the budget to prepare playback, waveform metadata and peaks could
be admitted. Playback Stop correctly retained that GUI data, but the test waited
for the earlier smaller total. A diagnostic reproduction on the current source
identified the exact Stop-accounting wait; its original failure is retained.

Commit `1e6c684f4ee345d8a99b540a181f18d9d5874ace` waits for the fixture's one
shared waveform before establishing the baseline. It also awaits waveform
redisplay after the explicit routing edit. The test still requires exact ledger
accounting, pre-activation refusal, retry, execution retirement, and zero owners
after window destruction. Its CMake target now depends on the waveform worker.
Timeout diagnostics include the calling line; no deadline or memory gate was
relaxed. No production audio or GUI code changed in this repair.

The corrected fixture passes **80 checks in 1.72 seconds**. The one-track ledger
returns from 6,937,886 prepared bytes to its 6,544,142-byte baseline. The 512-track
file/EQ case retires from 431,794,708 to 10,672,516 bytes; the explicit routing edit
accounts for the changed project/history baseline. It still verifies float
headroom, exact generated samples, saved state, and owner release on close.
The initial diagnostic failure, a compile-time widget lookup correction, and the
premature redisplay assertion failure are preserved with the final passing log.
Hosted verification of this repair remains a separate gate.

## Native Windows preparation

Original CI artifact `12012412896` was checked against its API size, digest and
ZIP CRC. A separate exact-source checkout with Windows line-ending filters
matches all **1,845** native source inputs. All **16** deployment files match the
collection manifest. The reviewed Microsoft runtime hash, signature and existing
version are verified in the guest; no runtime installation was performed.

Only the existing independent `soundcurrent-win11-dev` clone ran, for roughly
five minutes. No other VM was active. Original VMs and the pristine template were
untouched. Native inspection/media/copy helpers, stretch-v4 and protected-v5
qualification passed sequentially. Actual desktop launch used the normal Windows
Qt platform and exited normally with code zero, without forced retirement. The
receipt binds both the comp-span and waveform helper hashes. The 1016 by 679
window was visually inspected. This launch check does not establish installed
editing or audio-device correctness.

All owned guest processes finished, the temporary scheduled task was removed,
evidence was collected and verified, and the VM shut down gracefully. No physical
audio was activated. All VMs are off.

A lightweight progress poll ran before setup finished and reported a missing
setup receipt. A subsequent poll replaced that observer log; the first diagnostic
remains in the chat output, not as an original retained file. Setup finished before
helper dispatch. Product qualification and CI logs are separate and retained.
The host runner now refuses existing log paths. This observer limitation does not
qualify any unobserved workflow.

## Candidate and next workflow

The candidate is unsigned, with the reviewed Microsoft runtime kept intact.
Its complete corresponding-source archive is checked for tracked membership,
native input bytes, remaining Git blobs/filters, digest and gzip CRC. The local
installer is not a published release and has not yet been exercised.

Next: install this exact candidate on the independent installer-test clone, then
verify source waveform channels, absent-channel display, restored-media retry,
editing, Undo/Redo, save/reopen, export and normal close. Keep that VM session
brief and leave the installed Linux preview stable. The installed Windows preview
currently remains the earlier `97e9ecdc` build.

See the [acceptance manifest](../tests/results/M2/2026-10-15-windows-waveform-preview/manifest.json)
for source identities, original logs, command receipts and explicit gaps. Dense
waveform qualification, fine-window/large-file support and the remaining complete
DAW scope continue independently; none is declared complete here.
