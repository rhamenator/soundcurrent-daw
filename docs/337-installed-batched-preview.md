# Installed Linux preview with batched comp verification

Checkpoint 337 prepares and activates Ubuntu preview
`0.1.0~preview.20261015192606.97e9ecdc32cf`, from source
`97e9ecdc32cfaddca03e4c8df9829fbdcb68c0aa`. It incorporates the
[batched verification change](335-batched-comp-verification.md), whose focused
native Windows checks are recorded in [checkpoint 336](336-windows-batched-comp-verification.md).

## Build and package qualification

The cached Release build updates with one compiler worker. All **27 existing
preview checks pass in 229.70 seconds**, including the actual stretch/protected
workers, recording panel, repeated comp transitions, localization infrastructure,
project state, media integrity and export. This is the selected preview gate,
not the complete DAW acceptance matrix.

The local DEB is **7,107,800 bytes**. Every extracted payload matches the qualified
build and source. Its separate corresponding-source archive is **2,513,891,014
bytes**; all **8,101 tracked files** match the frozen commit or its declared Git
text filters, with complete membership, archive SHA-256 and gzip CRC checked.
The package and source archive remain local; no binary release is uploaded.

## Installed public-control workflow

The candidate is installed beside the previous preview, then tested using a
private display, session bus and PipeWire service with only Dummy/Freewheel nodes.
No VM or physical audio route is used.

Public controls set two prepared clips to 1.5× speed, render an equal-power
transition using extended processed handles, Review, Apply, Save, Undo, Save,
Redo and Save. Rendering leaves the arrangement unchanged until Apply. Undo
restores the original tracks; Redo restores the exact applied project. A second
normal launch reopens that state and exports through the WAV destination control.

The render observer completes in **4,316 ms**, with the displayed elapsed value
frozen at **3.8 seconds**. The previous installed run of this fixture recorded
5,496 ms and 5.0 seconds. These are individual observations, not a controlled
multi-run benchmark or a general latency guarantee. The Windows installed render
still requires independent measurement of the new implementation.

The 48 kHz stereo float export contains **18,032 frames** and matches the
independent gain/fade/mix calculation **bit for bit**, maximum error zero. Its
SHA-256 also matches the previous preview's export:
`70a071120e26b4aa4a2b5cdd4a220d0932c604d5ad926ac9834067201894af6b`.
All 180 original non-project fixture files, all 19 installed payloads, and the
previous preview's files remain unchanged. Both directly launched applications
quit normally; their workers and private audio services retire.

## Applications entry and rollback

After the workflow passes, **SoundCurrent DAW Preview** in Applications advances
to this version. **SoundCurrent DAW Preview (before batched verification)** opens
the preserved `b5dc256` version. Existing older launchers remain intact. Desktop
entry validation passes, and a third launch through the Applications entry opens
the exact candidate and saved project, then quits normally.

The [evidence manifest](../tests/results/M2/2026-10-15-installed-batched-preview/manifest.json)
binds 1,715 archive entries, including drivers, snapshots, package/source receipts,
test logs and sample-verification results. ZIP and payload hashes pass. Application
binaries, generated audio and private preferences are excluded from that archive.

## Next work

Hosted [CI 38792511012](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38792511012)
on the frozen source remains in progress at this checkpoint. Retain its original
terminal logs and artifacts before advancing the draft PR. Next prepare the
matching Windows preview and repeat its actual installed rendering workflow.
Clean-machine/package installation, sustained hardware operation, remaining
interruption/protected-warp findings and full-suite parity remain open.
