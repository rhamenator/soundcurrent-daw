# First Linux preview candidate

This is an early **Ubuntu 26.04 amd64** candidate. It is a Debug build with Linux
PipeWire enabled, prepared locally with its corresponding GPL source. Windows,
Fedora/RHEL and other Ubuntu versions are not qualified by this package.
No GitHub binary release has been uploaded.

The latest local candidate is **0.1.0~preview.20261008034000.743392ece10a**, in
`.cache/preview-portable-faults-ubuntu-26.04/`. It adds saved single-track error
details after reopen and passes an actual installed fault/Save/Quit/reopen/review
workflow. See [its package hashes, installation and evidence](93-installed-portable-fault-preview.md).
The same binary also passes [normal 11.264-second recording, live EQ/Undo,
save/reopen and independently verified WAV export](94-installed-normal-preview.md).
The two earlier candidates below retain their original, different test scopes.

## Package and installation

Recording/EQ/export workflow candidate: `0.1.0~preview.20261008001000.85ec9552cdd7`

- Application: `soundcurrent-daw_0.1.0~preview.20261008001000.85ec9552cdd7_amd64.deb`
- SHA-256: `dd579bfd42cba5b21ff1b6de7588bcb260f395f63997a97338162d777a73a6e2`
- Corresponding source: `soundcurrent-daw-0.1.0~preview.20261008001000.85ec9552cdd7-source.tar.gz`
- Source SHA-256: `62f9f7b28aac28bf1e9c24096730b03363785c4627cc2cea7a6f6c8788b19ebc`

The two artifacts are prepared together in the maintainer's local
`.cache/preview-ubuntu-26.04-sequenced/` directory. Source is also available at
[the exact commit](https://github.com/rhamenator/soundcurrent-daw/tree/85ec9552cdd7161f08d8b189d842258241747b5e).

No compiler or Qt SDK is needed. Runtime dependencies are declared in the DEB;
initial installation may require network access to Ubuntu package repositories.
The tested package-manager route, from the directory containing the DEB, is:

```sh
sudo apt install ./soundcurrent-daw_0.1.0~preview.20261008001000.85ec9552cdd7_amd64.deb
```

Installation needs package-manager elevation; launch the application as your
normal desktop user. The package includes an Applications entry and icon.
Launch `soundcurrent-daw` if checking the candidate before complete app-menu
qualification. It uses existing PipeWire; no virtual cable or new audio driver
is required. The package does not select audio devices or replace a daemon.

Close the app before changing its installed package. The current package does
not yet implement a running-recording upgrade guard.

## New diagnostic candidate

Version: `0.1.0~preview.20261008013908.97a307fcf2ba`.

- Application: `soundcurrent-daw_0.1.0~preview.20261008013908.97a307fcf2ba_amd64.deb`
- SHA-256: `9f99486260f4ccec270c7e6bf8d21a3960f6c58c3e6970bda98655885ec43abf`
- Corresponding source: `soundcurrent-daw-0.1.0~preview.20261008013908.97a307fcf2ba-source.tar.gz`
- Source SHA-256: `b2d8979367ff7a0d46f357c8f759953f1ffc9dbf478b634ea3399dbad29ae15d`

Both are local in `.cache/preview-recording-faults-ubuntu-26.04/`; all 710 tracked
source files were independently checked against
[the exact commit](https://github.com/rhamenator/soundcurrent-daw/tree/97a307fcf2bae9ea2781de96a4e25dc76dbda001).
From the directory containing the package, with the app closed:

```sh
sudo apt install ./soundcurrent-daw_0.1.0~preview.20261008013908.97a307fcf2ba_amd64.deb
```

This candidate adds precise recording failure explanations with current/previous
clock details. Normal upgrade and an actual installed-GUI short-failure workflow
pass in the owned Ubuntu rootfs: 1,024 raw frames survive, are verified and attach
to the saved project. Detailed fault data remains in the recording session;
reopening currently retains the journal end reason and timing origin only.

Required hosted Linux checks pass 58/58; Windows passes its core cross-build only.
The earlier candidate's successful ten-second recording/EQ/reopen/export evidence
does not establish the same installed workflow for this newer binary. See the
[installed diagnostic receipt](../tests/results/X007/2026-10-08-installed-recording-fault.json)
and [diagnostic contract](91-recording-fault-diagnostics.md) for exact scopes.

## Try the recording workflow

1. Use **File → New project…** or **Open project…**. Keep media with its project
   folder. Create/select a track in the timeline.
2. Use **Prepare recording**, choose the input and any monitoring output explicitly,
   and **Arm selected track**. Preparation and route selection do not start recording.
3. Use **Record**, then **Stop recording**. The app verifies the raw take before
   attaching it. Wait for any take-verification message to finish.
4. Prepare playback and explicitly choose its output. Play the take and adjust
   the selected track EQ. **Edit → Undo project edit** reverses an accepted gesture.
5. **Save**, close, reopen the project folder and check its take/settings.
6. Use **File → Export WAV…** to choose range/destination and export a snapshot.
   Rendering uses the same in-process EQ. Existing destinations require confirmation.

Raw recordings are kept before track EQ; changing EQ does not rewrite the raw
WAV. The equipment library/editor is available, but profile processing/monitoring
integration is still a separate unfinished workflow. Language and number-format
preferences are under **Settings**; 32 non-English catalogs contain only partial
unreviewed drafts and use English fallback.

## What the candidate checks establish

| Check | Evidence and scope |
|---|---|
| Runtime installation | Fresh signed Ubuntu Base 26.04.1 derived rootfs resolves dependencies without compiler/CMake/Qt SDK |
| Project and export | Installed GUI, normal UID/GID 1000, Unicode project opened/saved/reopened; 128 mono float frames at 48 kHz exactly match the golden render |
| Installed recording | Explicit owned WAV-player input, mono 48 kHz, 493,568 raw float frames (10.28 s), normal stop and verified attachment; 2,048 leading silent frames remain a startup/alignment gap |
| Live EQ and Undo | Actual GUI change −12 to −6 dB and Undo; independently recorded output contains stable levels in the expected order; raw take unchanged |
| Recorded-project export | 480,000 frames match an independent direct-form I EQ calculation within 2.85e−14; reopening preserves project/media bytes and produces identical export bytes |
| Package lifecycle | Normal upgrade, remove and reinstall; owned project/media bytes preserved; integration files removed/restored |
| Regression | 62/62 local Linux Debug checks; changed Python/desktop 3/3; earlier affected sanitizers and hosted Qt6.4.2 checks recorded separately |

The container shares the host kernel and uses private Xvfb/PipeWire, not a complete
fresh Ubuntu desktop. The installed-app workflow above uses an owned source and
sink, with no physical audio device or real-time scheduling qualification. Menu
launch, Wayland/HiDPI, physical latency, sustained sessions, setup failure/recovery
and native Windows remain open. No frozen Bitwig/Cubase parity family is complete.

Two recording limitations are retained in the evidence: an earlier audiotestsrc
route stopped with a clock discontinuity after 1,024 frames, and the successful
take starts with 2,048 silent frames (42.7 ms). Every subsequent raw sample matches
the owned source period exactly, but a periodic signal cannot independently detect
loss of whole periods. The newer diagnostic candidate reproduces the audiotestsrc
clock staying at position0 for the next block instead of advancing to1024. The
old opaque fault and startup alignment still need further work; keep original
takes and treat these as development previews.

For removal, close the app and use the normal package manager to remove
`soundcurrent-daw`. Scoped remove/reinstall testing preserved the owned project
and media. Broader preference/recoverable-take and interrupted-update tests remain
required. Keep the supplied source with the candidate when redistributing it.

The [runtime receipt](../tests/results/X007/2026-10-08-preview-runtime.json),
[installed workflow receipt](../tests/results/X007/2026-10-08-installed-preview-workflow.json)
and [delivery gates](89-workflow-previews.md) record hashes, original failures and
remaining work. Next: portable failed-job diagnostics and a tested first-valid
input/alignment policy, then complete desktop and native Windows qualification.
