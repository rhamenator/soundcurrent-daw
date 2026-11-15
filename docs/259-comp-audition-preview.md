# Ubuntu comp and take-audition preview

Checkpoint 259, 2026-10-13. The full DAW goal remains active.

## Available on this workstation

Open **SoundCurrent DAW Preview** from Applications. This updates the per-user
launcher to the Ubuntu 26.04 amd64 Release preview containing retained take audition,
paired crossfades and transitions made from retained source handles. Select a
retained lane and **Prepare take audition**, choose outputs, then Play. Stop and
normal Prepare playback return to the arrangement. Preparation starts no output.
Use **Clip gain and fades → Crossfade clips…** and explicitly select retained-take
handles when adjacent comp fragments need overlap. Missing source handles and
separate unprepared stretch derivatives remain refused.

**SoundCurrent DAW Preview (previous)** retains the previous installed version.
The exact original launcher and both independent payload directories are retained.
The update validates copied package bytes and desktop entries before atomically
replacing the main launcher. No system package, daemon configuration, physical
route, VM or equalizer repository is changed. No release binary is uploaded.

The DEB and exact GPL source remain local. This qualifies per-user extracted
installation on this workstation; it does not rerun a clean system-DEB lifecycle,
Fedora/RHEL or other Ubuntu versions.

## Exact candidate

- Source: `1a3b2022cffa45fbe2040ef251ed1d57b7856213`.
- Version: `0.1.0~preview.20261013210831.1a3b2022cffa`.
- Local artifacts: `.cache/p259-package-20261013210831/`.
- DEB: 4,273,666 bytes; SHA-256 `3d8f3cfa38901c5be9d235c9c74fbd57e0eeb1dc158e968f28d9e573a1b07bac`.
- Corresponding source: 880,773,996 bytes; SHA-256 `aea91b96d0d62d033d218831eb70c06489f9b9c6ac14fa798d237616bc080da8`.
- Installed main executable: SHA-256 `ddbda4c30dee7e9622684e5c0b7599e1295cd264efb6e56b146d08b06e7c0f45`.

The builder checks the complete install payload, dependencies, notices, modes,
source inputs, actual current-source stretch workers, package extraction and
help/version startup. Debian version ordering is checked against the retained
`0.1.0~preview.20261013064632.5a2046905c77` predecessor.

## Acceptance

[Indexed evidence](../tests/results/X007/2026-10-13-comp-audition-preview/index.json)
retains commands, original failures, receipts, screenshots and owned synthetic
project/PCM for independent comparison. No executable, SDK or installer is in that
public evidence archive.

| Workflow | Observed result | Limits |
|---|---|---|
| Corrected native Linux preview gates | 23/23, 61.07 seconds | Selected gates, not the whole native test suite |
| Actual v4/v5 stretch helpers | New source-bound process/artifact receipts; protected bank 33 workers, 21 completed renders, 517 checks | Synthetic isolated workers; full processing quality remains open |
| Private native playback | 480,000 frames, live EQ and Undo, live/offline difference 0, no missing frames, callback allocation/free/locks 0 | Dummy/freewheel routes; no physical output |
| Private native recording | 99,072 bit-checked raw frames, 8,192 monitor/sink frames, admitted take and save/reopen; callback audit 0 | Existing PipeWire SDK; synthetic source/private daemon |
| Reopened admitted playback | 8,192 frames, explicit 257-frame leading silence, live/offline error 0 | Owned recorded fixture, not physical microphones |
| Packaged take preparation | Take-specific status, no selected endpoint/Play; original project/media hashes unchanged | Passive preparation and normal return; physical listening pending |
| Packaged handle crossfade | 239,880–240,120 equal-power transition; exact IDs/origins/lanes/raw retained; Undo/Redo/Save/reopen pass | Two distinct synthetic mono sources; broader reference comp workflows remain required |
| Packaged exports | Two 480,000-frame Float32 WAV files byte-identical; independent sine/cosine crossfade and direct-form-I EQ error ≤1.1899427665440498e-7 | Scope of this owned fixture; peak 2.439275026321411 preserves headroom |
| Applications entry | Exact installed executable launched through gtk-launch, normal exit 0 | Private Xvfb interaction; native Wayland input/other desktops remain open |

The corrected [original CI run 38468072593](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38468072593)
for exact source `1a3b202` passes all four jobs: Linux 165/165, native Windows 90/90,
Windows desktop 22/22 and core cross-build. All original artifact ZIPs are retained
locally, including the 123,601,224-byte deployment-input ZIP. Independent audit
verifies 14 deployed files, 1,509 source files against actual Git Windows checkout
filters, every recorded source-archive/artifact hash and both reviewed media DLL
builds, with no dependency mismatch. These are current-source unit/Qt/helper and
byte/source results. The native main-window/audio/installer flags remain false.

## Original failures and remaining work

Original source `9ac1386` failed hosted Linux 2/165 and local preview 1/22 because
Qt source inventory still recorded the pre-retirement-fix playback controller.
The other hosted Linux 163 tests and Windows native 90/90, desktop 22/22 and
cross-build passed. Actual `tools/localization.py --update` regenerated extraction
and compilation; only the controller fingerprint changed, with catalog bytes and
1,038 keys unchanged. The corrected run adds the deliberate catalog-corruption
failure gate locally. Original receipts/PIDs are preserved rather than rewritten
for the corrected source.

Desktop observation failures are retained. Combining xdotool PID/title predicates
with `--all` fixed an incorrect window selection. The first keyboard Return/Save
sequence left the modal open; explicit OK and visible File/Save actions qualified
the intended workflow. The longer handle explanation clipped at the initial small
dialog size and became readable after resizing; responsive help layout remains a
follow-up. The final menu driver lost its private X connection as the runner joined
Xvfb after actual application exit 0; no forced app termination qualified the result.

Windows source audits must respect actual checkout filters: raw Git blobs may
use LF while native checked-out text uses CRLF. Bounded actual Git checkout-filter
outputs and every corresponding-source archive byte were compared with native
snapshots; dependency identities and two deterministic media builds are checked
against independently reviewed pins. This is byte/source audit, not native startup
or installer qualification. Original failed audit attempts remain local.

Localization remains 34 partial catalogs, 3,168 drafts, zero native-reviewed or
fully UI-qualified languages. Native Windows main/route/installer acceptance,
physical and sustained recording, stretch quality, responsive small-dialog help,
all remaining M2–M11/F/Q/C/N/X004/X005 and all-Europe qualification remain required.
The Windows console-login question remains pending; VMs are off. Continue independent
work without password guessing/resetting or changing equalizer repositories.

## Next implementation task

Render and admit matching source handles for separately stretched/warped comp
fragments through the existing asynchronous worker. Preserve raw anchors, take
provenance, processing settings, explicit source context, cancellation and safe
owner retirement. Keep absent handles refused until actual verified artifacts
exist. Repair the observed small-dialog help layout with real Linguist regeneration
and UI evidence, then continue the full staged backlog.
