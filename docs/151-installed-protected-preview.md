# Installed protected stretch preview observations

2026-10-10 UTC. Full-suite completion remains open. This follows
[checkpoint150](150-protected-preview-gates.md) and normally protected
[PR91](https://github.com/rhamenator/soundcurrent-daw/pull/91), squash
`8b46e8975e832edc3fc8c19b1c62d3edf96277e2`, tree
`fe3197c30e20c30493f03eb9ba9720a12f7de2e2`. The final tested PR head was
`083aefed3afd027406e1bb78e6f2d66b2f4dbfd8`; the squash has the identical tree.
Final hosted gates passed 115 Linux, 46 native Windows core and 14 native Windows
Qt tests plus Windows cross-compilation. The exact source also passed 118 local
native-enabled tests. The required checks and resolved review threads admitted a
normal protected merge; no bypass or protection change.

## Actual installed Linux scope

Three temporary Ubuntu overlays used their own network, private PipeWire,
virtual display and writable overlay. The installed app/helper ran as UID 1000
with no effective capabilities and NoNewPrivs. No `/dev/snd`, host package/audio
changes or original VM/equalizer changes. Every application and overlay exited 0.

The first/reopened overlays installed an earlier PR91 head `b0e61308e053`.
The third installed that package and actually upgraded it to the final tested
`083aefed3afd` package. All five installed executable hashes matched the final
package and were byte-identical to the first observed render workflow. The Python
receipt-checker corrections changed source identity, retaining compiled payload
identity; the reports keep both exact commits rather than rebinding observations.

The default installed sibling helper rendered owned stereo float audio at 48 kHz,
32,768 input frames, 3/2 duration, with an owner marker 4096→6144. Editing the
marker after Render disabled Apply/audition; attempted disabled clicks left the
saved project and private graph unchanged. Restoring the verified inputs admitted
them again. Prepare audition held an endpoint; Stop retired it. Actual audition
Play was not activated. Apply/save, Undo/save and Redo/save preserved independent
original/applied project states. Unicode save/reopen retained exact project bytes
and marker controls. All three exports were byte-identical: 49,152 frames with
PCM identical to the derivative and 1.5 peak headroom. Each private graph returned
to its four initial owned nodes.

These observations qualify this bounded installed workflow. They do not qualify
physical audio, native audition Play, cancellation/hard-exit/restart recovery,
sustained multichannel phase, broad stretch quality, all F/Q/C/N requirements or
European translation/review/UI coverage.

## Retained and corresponding-source evidence

[Retained results](../tests/results/M2/2026-10-10-installed-protected-preview/README.md)
contain bounded original synthetic WAVs, saved project states, raw process/exit
logs and screenshots. The portable verifier inspects exact member hashes, full
float PCM, saved history/reopen, helper identity and graph retirement. It runs no
captured scripts, installation or audio. System configuration copies and bytecode
are explicitly filtered; executables, installers and personal recordings are absent.

The final local DEB is version
`0.1.0~preview.20261010095352.083aefed3afd`, SHA256
`067abefdd87e554fd33712731aa051807e152908a97e6509c4e7ea0d3b10ed39`.
Its matching source archive SHA256 is
`91b6e06cbb4c0b82b69350dcb403a9aa363a512d330e6741e4a71173a0cba772`.
All 2,179 archive blobs matched the exact Git export policy, including the explicit
PowerShell CRLF rule. Source archives/binaries remain local; source/evidence
repository backup is separately owner-authorized. No product release upload.

## Next implementation and delivery gate

The independent Windows development clone built exact merged source successfully
with two workers, four CPUs and a firm 6 GiB cap. Actual inspection, approved WAV
and copy recovery process receipts succeeded. The initial qualification harness
used an obsolete copy-receipt key; that failed observation is retained, and a
fresh recovery process verified the correct source/staged hashes.

The resumed v4 stretch bank then returned a failure with empty stderr; it produced
no completed qualification report. Default desktop startup did not expose a
window within either observation deadline. A diagnostic Fusion style override
also failed to expose a window. This does not establish a theme cause. These
failures, raw process/module observations and incomplete scope remain explicit;
v5 and Qt workflow qualification did not proceed. No refreshed Windows installer
was prepared or uploaded. The owned startup processes were retired and all VMs
were authoritatively stopped before the evidence checkpoint.

Next, reproduce the initial native helper request with retained stdout, typed
events, exact process exit and module identities; isolate desktop initialization
with bounded probes. Resolve those failures before refreshing the Windows
installer and independently testing
installed Unicode marker render/edit/audition/Stop/Apply/Undo/Redo/reopen/export.
Then add parent-owned persisted render recovery with exact clip/project guards,
preview/review on restart, cancellation and abnormal-exit qualification. A retained
helper intent alone cannot safely reattach a result after application restart.
Continue M2 and all remaining staged full-suite requirements afterward.
