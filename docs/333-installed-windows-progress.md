# Installed Windows preview with render progress

Checkpoint 333 installs the exact [checkpoint-332 candidate](332-windows-progress-preview.md)
on the existing independent Windows acceptance clone and exercises the public
editing workflow. The source remains
`b5dc2568dbf0e1e623dbaa7d42dcd38fd1eed3ab`; later documentation and CI changes do
not change the installed binary's provenance.

## Installation and preserved rollback

The unsigned x64 installer exits zero. All 65 installed payload hashes match,
and the desktop shortcut, Start Menu shortcut and uninstall registration pass
inspection. The existing Microsoft runtime is sufficient; it is not reinstalled.
All 334 files belonging to earlier previews remain unchanged. The new preview
stays installed alongside them. Neither the compiler nor qmake is on PATH.
This is an existing acceptance OS, not a pristine clean-install qualification.

The independent clone is used alone. No physical audio endpoint is activated.
Both actual application launches close normally with exit zero, and no owned
application or helper process remains. The VM shuts down normally. The temporary
capture receiver stops, its firewall rule is removed, the CD is ejected and the
two owned transport ISO copies are removed. All VMs are off; original VMs and
the pristine template were not started.

## Actual editing and independent export

The input is the same owned Unicode-path Linux project used in the previous
installed Windows check. All 204 original project files match their inventory.
The installed application regenerates a transition from its Linux processing
history, including the original assembly identifier. The two new source spans
contain 4,064 stereo frames each and have native Windows process-completion
receipts bound to the installed worker hash.

Public controls complete Review, Apply, Save, Undo, Save, Redo and Save. Undo
restores the original tracks exactly; Redo restores the complete edited state.
After a normal close, a second actual launch reopens that saved state and exports
the selected 0–18,032-frame range. Original history/media files remain unchanged.

The exported float WAV contains 18,032 stereo frames at 48 kHz. Every sample
matches the independent reference bit for bit (maximum error zero), including
floating-point headroom: peak **5.5167012214660645**. This synthetic peak is not a
physical playback or listening result. The 144,344-byte export SHA-256 is
`5a88856659abc1fdebb48ebdb452d6948fa9e5bfaffc898e2d7cf3dfeb9bcbac`.
It also matches the previous installed Windows export.

## Original driver failure and unresolved responsiveness

The first automation driver fails after starting the actual render because a
transient UI Automation element has a null AutomationId. Its exception and
failure report are retained. The product continues rendering in the same process;
no second render is started to replace this observation.

A separately recorded continuation uses a null-safe accessibility query and
attaches to that same process and render. Screenshots show the source-rendering
stage at **26.2 seconds** and the completed, verified transition at **34.9 seconds**.
The displayed elapsed value remains fixed at completion. The continuation then
performs the editing/export workflow above successfully.

The original driver did not complete its render wait, and the observed whole
render exceeds the earlier 30-second expectation. `originalRenderWaitPassed`
remains false; `renderMilliseconds` is null because the driver did not capture a
complete independent start/end measurement. The displayed 34.9 seconds is an
observed UI wall time, not a per-phase CPU or disk measurement. The native build
is Release with `/O2`; this does not establish the cause of the delay. The earlier
protected-warp failure also remains unresolved. Neither deadline nor verification
requirements have been relaxed.

## Evidence and next work

Evidence: `tests/results/M2/2026-10-15-installed-windows-progress/acceptance.zip`,
85 entries, 2,770,430 bytes, SHA-256
`2a18735f6437bda4c2d31af658dd5f4b0dd01d2f0498a618678e4337cf19a8ae`.
ZIP CRC and every payload hash pass. The 225-file final project is reconstructible
from new entries and the hash-pinned checkpoint-323 base. The archive retains the
initial driver failure, successful continuation, installation/preservation
receipts, independent sample checks and cleanup observations. Application
binaries, transport credentials and original capture ZIPs remain local.

Hosted [run 38782533935](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38782533935)
was still running when this evidence was captured. Its new retained-evidence job
passes, but a partial run is not an aggregate pass. Preserve its original terminal
logs/artifacts before advancing the draft PR.

Next measure the Windows render phases and process CPU/I/O costs with bounded
instrumentation before choosing a performance change. The installed workflow is
usable, but the responsiveness finding remains open. Full DAW functional and
quality parity, broader installation/hardware coverage, content and European
localization remain incomplete.
