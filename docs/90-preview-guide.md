# First Linux preview candidate

This is an early **Ubuntu 26.04 amd64** candidate. It is a Debug build with Linux
PipeWire enabled, prepared locally with its corresponding GPL source. Windows,
Fedora/RHEL and other Ubuntu versions are not qualified by this package.
No GitHub binary release has been uploaded.

## Package and installation

Candidate: `0.1.0~preview.20261008001000.85ec9552cdd7`

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
| Package lifecycle | Normal upgrade, remove and reinstall; owned project/media bytes preserved; integration files removed/restored |
| Regression | 62/62 local Linux Debug checks; changed Python/desktop 3/3; earlier affected sanitizers and hosted Qt6.4.2 checks recorded separately |

The container shares the host kernel and uses private Xvfb, not a complete fresh
Ubuntu desktop. Actual installed-app capture/playback, menu launch, physical
latency, sustained sessions, setup failure/recovery and native Windows remain
open. Earlier owned native/synthetic recording evidence is not a fresh-package
recording result. No frozen Bitwig/Cubase parity family is marked complete.

For removal, close the app and use the normal package manager to remove
`soundcurrent-daw`. Scoped remove/reinstall testing preserved the owned project
and media. Broader preference/recoverable-take and interrupted-update tests remain
required. Keep the supplied source with the candidate when redistributing it.

The [runtime receipt](../tests/results/X007/2026-10-08-preview-runtime.json) and
[delivery gates](89-workflow-previews.md) record exact hashes and remaining work.
Next: installed-app ten-second recording/EQ/playback/save/reopen/export on owned
routes, followed by complete desktop install/upgrade/remove qualification.
