# Updated Ubuntu recovery preview

Checkpoint 250, 2026-10-13. The full frozen-reference DAW goal remains active.
This delivers a usable Ubuntu preview; it does not complete M2, platform parity,
processing-quality parity or European language coverage.

## Delivered candidate

- Version: `0.1.0~preview.20261013064632.5a2046905c77`.
- Exact application source: [5a2046905c77aecc368837919d38f8b720083893](https://github.com/rhamenator/soundcurrent-daw/tree/5a2046905c77aecc368837919d38f8b720083893).
- Ubuntu 26.04.1 amd64, Release, Qt 6.10.2 and PipeWire 1.6.2.
- DEB: 4,262,266 bytes; SHA-256 `3c4aa658dfaefcb628244648de1553d1e29f793604811125912e4a664a1fa7ab`.
- Corresponding source archive: 878,982,235 bytes; SHA-256 `411e827251c7958feb66e44e0d3c6a777d9616c296e72dc4326008d5d31f3eb8`.
- Both artifacts remain local in `.cache/p250-package-20261013064632/`.

The candidate includes the [reviewed recording recovery controls](249-recovery-copy-desktop.md),
including finite-prefix previews, explicit copies, changed-checkpoint refusal and
cancellation. It also contains the earlier editing, isolated import/stretch helpers,
project, recording, in-process EQ and WAV-export work. It uses existing PipeWire.
No new driver, cable, daemon implementation or dependency was introduced.

The builder verified the complete CMake install payload before stripping its copied
main executable. It declared runtime dependencies, validated the desktop entry,
extracted and checked every payload byte/mode, and ran packaged help/version.
The exact source archive comes from the clean tested commit. Debian version ordering
is verified against the newer retained predecessor
`0.1.0~preview.20261010165009.5300010ab521`; the earlier checkpoint's predecessor
reference had missed that candidate. The application itself still reports the
base version 0.1.0; the package version identifies this preview precisely.

## Installation on this workstation

**SoundCurrent DAW Preview** is now available in the normal user's Applications menu.
The registered launcher is `~/.local/share/applications/soundcurrent-daw-preview.desktop`.
Its executable, adjacent helpers, notices and icon are exact extracted package bytes
under `~/.local/opt/soundcurrent-daw/previews/<version>/usr/`. The launcher uses a
quoted absolute executable path and an absolute icon path. It does not depend on
CMake or a development build directory. No system package was installed and no
administrator access was used. The version directory remains independent of source
branches and future builds.

This was the first DAW launcher on this host, so no host-installed candidate was
replaced. Earlier installed-preview evidence came from owned rootfs/VM environments.
Updating a future user launcher must retain the previous version and its launcher
before switching it. Removing this preview means removing its dedicated launcher
and this exact owned version directory; projects and source/package artifacts remain.
The separate DEB supports ordinary package-manager installation on the qualified
Ubuntu version, but this candidate's clean system-DEB lifecycle was not rerun.
No release binaries were uploaded.

## Current-source evidence

[Indexed logs, receipts and two screenshots](../tests/results/X007/2026-10-13-recovery-preview/index.json)
retain exact source, executable, package and original CI identities.

| Workflow | Actual result | Scope |
|---|---|---|
| Selected Linux preview gates | 16/16, 55.01 seconds | Recovery panel, raw prefix handling, localization extraction, project state, staging, import/transaction helpers, WAVE validation, export and v4/v5 stretch workers |
| Protected stretch worker | 33 actual worker processes, 21 completed renders, 18 complete PCM comparisons, 517 checks | Native Linux isolated helper; full sonic-quality parity remains unqualified |
| Private native playback | 480,000 frames, live EQ and Undo; live/offline difference 0; no missing frames | Actual installed PipeWire daemon in a separate socket namespace, dummy/freewheel only, no physical output |
| Private recording controller | 99,072 raw frames checked bit by bit; 8,192 monitor/sink frames; saved/reopened admitted take | Actual PipeWire SDK, fixed 256-frame quantum, explicit synthetic source and stereo matrix; durable outcome and eight evidence files |
| Admitted native playback/export | 8,192 frames, 257 explicitly placed leading-silence frames, live/offline difference 0 | Uses the reopened admitted take; callback allocation/free/blocking-lock counts are zero |
| Applications-menu launch | Actual packaged executable opened the Unicode-named project on this host's desktop | Exact running binary hash; shared-desktop automated input is not qualified |
| Packaged Save/Quit/reopen/export | Two actual menu-launched application processes exited 0; repeat WAV exports identical | Interactive operations on private Xvfb; unchanged existing project/media, matching `project.previous.json` created by Save |
| Independent export calculation | 480,000 mono Float32 frames at 48 kHz; maximum error 1.1899427665440498e-7 | Independent Python direct-form I peaking EQ using actual raw samples; sample peak 2.439275026321411, preserving headroom |

The original [CI run 38356229199](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38356229199)
for this exact source passed all four jobs: Linux 161/161 in 463.69 seconds,
Windows native units 88/88 in 274.06 seconds, Windows desktop 20/20 in 75.72 seconds,
and the core cross-build. The Windows recovery panel passed in 15.90 seconds.
Original terminal APIs/job logs and original Windows artifact ZIPs are retained
locally, with archive identities indexed publicly. These qualify the declared unit,
helper and Qt workflows. They do not establish physical WASAPI recording or a new
Windows installer. The returned desktop artifact lacks a current deployed application
bundle/manifest; the native artifact's worker receipts alone cannot satisfy the
Windows package builder's complete deployment requirements.

## Original failures and limits

The first private playback attempt stopped before invoking its fixture because the
Unix socket path exceeded 108 bytes. A shorter owned output directory passed without
changing callbacks or deadlines. The first recording diagnostic used an older minimal
daemon configuration; `pw-dump` timed out before fixture invocation. A fresh private
copy of the installed configuration, at the fixture's required 256-frame quantum,
passed with only dummy/freewheel nodes before and after.

The first user desktop entry was correctly rejected because its version path's tilde
was unquoted in `Exec`. The original entry is retained; quoting fixed validation and
actual launch. Shared-desktop automation had unreliable window/input targeting; final
interactive operations used a separate Xvfb display and exact process/window matching.
No product UI was changed for the automation. An initial independent verifier assumed
RIFF-only input, although the raw source is RF64; its corrected reader handles both.
Its next assertion incorrectly treated the valid Save-created previous-project backup
as an unexpected change. The final check explicitly verifies unchanged original files
and an added backup matching the saved project. Original failure logs remain retained.

No VM, physical speaker/microphone route, system volume, equalizer repository, system
package or user audio connection was changed. Successful native recording fixture
media is deleted by the existing fixture; the execution log is retained, not offered
as replayable PCM evidence. Original failing fixture data remains retained where
created. No new sanitizer or sustained recording qualification is claimed.

Native Wayland input, HiDPI/menu integration on other desktops, clean installer/update
failure cases, physical latency, sustained hardware recording and updated Windows
installer qualification remain open. Localization remains 1,021 extracted keys and
34 catalogs with unreviewed drafts; no native-reviewed or fully UI-qualified European
language is claimed. Full M2–M11/F/Q/C/N/X004/X005 remains required.

## Next implementation task

Prepare a complete current-source native Windows deploy bundle and byte-bound helper,
Qt/runtime/license inputs for the existing package builder, then qualify a matching
unsigned per-user setup/source pair. Use hosted native builds first. Before any local
VM work, read the Copperfin policy, check that another VM is not active, and use an
independent clone for system-changing installation tests. Keep VM time brief and retain
actual setup/recording failures. Do not substitute these unit logs for deployment or
hardware evidence. Continue the remaining recording, editing and full-suite milestones
after this delivery work.
