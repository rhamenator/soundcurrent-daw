# Easy installation on supported platforms

Owner requirement **X007**, 2026-10-07. Linux and Windows are required platforms;
the frozen reference versions and all other acceptance requirements remain unchanged.
Status: product contract and staged work; no qualified binary installer exists yet.

A local Ubuntu 26.04 amd64 DEB candidate now has scoped fresh Ubuntu Base runtime
dependency installation, normal-user GUI project/export and upgrade/remove/reinstall
evidence. Complete desktop/menu, installed-app capture/playback and failure/recovery
gates remain open. See [the preview checkpoint](89-workflow-previews.md) and
[candidate guide](90-preview-guide.md); this is not full INSTALL-001 qualification.

## User experience

An ordinary recording-studio user must be able to download the appropriate package,
open it, install it, and launch SoundCurrent DAW from the applications menu or Start
menu. No compiler, SDK, Qt developer installation, terminal build instructions,
subscription, or account is required. Desktop shortcuts are available by choice.
The README's current CMake instructions remain developer instructions.

Setup handles application runtime dependencies or explains an unsupported system
before installation. Opening a project, choosing existing audio devices, and making
the first recording are discoverable GUI workflows. Application setup must not
require VB-CABLE or a new SoundCurrent audio driver. Native Windows audio is still
an implementation and qualification gap; this contract does not claim it works.
The equalizers are stable reuse references at the owner's request; their own
Windows drivers await a signing certificate. They remain separate products.

Updates preserve projects, media, preferences and recoverable recordings. Setup
must detect a running recording application and offer a clear retry after orderly
shutdown. Uninstall removes application files and integration while preserving
user projects and recordings. Any optional preference removal requires an explicit
choice. No installer silently replaces an existing audio server, changes system
output defaults, or installs additional virtual audio routes.

## Delivery routes and unresolved gates

| Platform | Intended installation route | Work needed before a supported release |
|---|---|---|
| Ubuntu/Debian family | Native `.deb`, installable through supported package-manager UI, with runtime dependency metadata | Pin supported distro/architecture versions, resolve dependencies on a fresh system, desktop/icon integration, audio/backend compatibility and upgrade/remove qualification |
| Fedora/RHEL family | Native `.rpm` for explicitly supported versions, with dependency metadata | Qualify each distro separately; Fedora compatibility is not RHEL compatibility. Distribution Qt/PipeWire floors, dependency names, plugin paths and support lifetime must be checked |
| Windows | A normal graphical installer with application runtimes included or handled automatically, Start menu/uninstall integration, optional desktop shortcut | Native Qt/audio application build, dependency/license inventory, installer framework decision, runtime deployment, standard-user/admin boundaries, Windows trust and native clean-install tests |
| Other Linux systems | Evaluate a self-contained distribution route after native packages | Compare AppImage/Flatpak or alternatives for audio/real-time/plugin access, device permissions, dependency updates and licensing; do not infer universal Linux support |

These are product directions, not selected dependency versions or package-generator
implementations. [CPack's DEB generator](https://cmake.org/cmake/help/latest/cpack_gen/deb.html)
and [RPM generator](https://cmake.org/cmake/help/latest/cpack_gen/rpm.html) are candidates
compatible with the existing CMake workflow; dependency metadata and distro tests
are still our responsibility. [Qt's Windows deployment documentation](https://doc.qt.io/qt-6/windows-deployment.html)
describes collecting Qt runtimes/plugins with `windeployqt`; additional third-party
dependencies need their own inventory. Current web documentation is not a toolchain
pin. Record exact tools and output hashes when an implementation is chosen.

Current `cmake --install` installs the executable, desktop entry, icon and selected
equipment notices. The separate DEB preview builder stages and verifies these files,
adds application notices and pairs exact source. CMake installation alone does not
produce an end-user installer, deploy Windows DLLs,
resolve every runtime dependency, or establish a complete GPL corresponding-source
delivery. Existing CI cross-builds Windows core code; it does not exercise a native
Windows Qt application or installer. Linux desktop CI disables native PipeWire.
The current PipeWire API floor is 1.6.2 and the Qt API floor is 6.4. Older supported
distribution stacks need a qualified adapter or an explicit support decision,
rather than requiring users to build or replace their audio stack.

Signing costs must not block independent user-mode development. Distribution trust
qualification is separate: preserve Windows security controls, disclose unsigned
development artifacts accurately, and qualify an affordable trust route before a
public Windows release. See [the existing signing decision](49-windows-signing-budget.md)
and [Microsoft's signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options).
No certificate purchase, store enrollment or release publication is authorized by
this installation requirement.

## Measurable acceptance workflows

| ID | Workflow | Required evidence |
|---|---|---|
| INSTALL-001 | Fresh supported OS without compiler/SDK/Qt development files: open one supplied package, install and launch from the normal app menu | Exact OS/package/build hashes, steps and prompts, runtime dependency report, visible window and first-recording/save/reopen/export evidence; no terminal build needed |
| INSTALL-002 | Install with a normal user; require elevation only for a justified system change | Owned clean-machine policy and permission results; application does not run as administrator/root; no audio default changes or unnecessary driver requirement |
| INSTALL-003 | Upgrade while an owned project, preferences and recoverable take exist | Preserve hashes/state and recoverability; running recording prevents unsafe replacement; next launch opens the existing project without loss |
| INSTALL-004 | Interrupted or failed install/upgrade: insufficient space, unavailable dependency, cancellation | Clear actionable message, no partial installation presented as usable, previous working installation/project/media preserved or a documented restoration path |
| INSTALL-005 | Uninstall and reinstall | Application integration removed, user media/projects/recovery untouched, optional preference removal explicit, reinstall can reopen/export previous project |
| INSTALL-006 | Localized installation and first run, Unicode user/install/project paths, HiDPI/small display | Per-platform/per-language installer and UI qualification distinct from translation completeness; no untranslated critical setup/error/recovery workflow hidden by a catalog-load pass |
| INSTALL-007 | Verify dependency and source delivery, platform trust and offline installation behavior | Exact transitive licenses/notices/SBOM/source/build receipts; dependencies bundled where lawful or obtained clearly; integrity hashes do not masquerade as a publisher signature; record any network requirement |

Tests use owned Linux environments and independent Windows VM clones under the
local VM policy. No fresh-install claim comes from the developer workstation,
Wine, a cross-build, or an archive copied to the owner's Windows machine.

## Staged backlog

1. **INSTALL-AUDIT**: inventory the actual executable/DLL/shared-library/plugin
   closure and supported OS floors; evaluate installer frameworks and licenses.
   Exit: exact dependency/support matrix and a recorded framework decision.
2. **INSTALL-LINUX**: implement native DEB/RPM generation and integration using
   the qualified distro backends. Exit: INSTALL-001 through 005 and 007 on every
   claimed distro/version; no replacement PipeWire daemon.
3. **INSTALL-WINDOWS**: after native Qt/audio workflows, assemble a redistributable
   runtime and graphical installer. Exit: the same workflows on owned Windows
   clones, with trust status explicit and no virtual cable prerequisite.
4. **INSTALL-LOCALES**: integrate reviewed setup strings, initial language choice,
   accessibility and Unicode paths. Exit: INSTALL-006 and per-language native/UI
   qualification; Europe coverage remains open until audited.
5. **INSTALL-RELEASE**: reproducible package/source artifacts, fresh install,
   upgrade, failure recovery and uninstall as recurring CI/release gates.
   Exit: all X007 workflows and platform/license/trust requirements qualified.

Start the dependency/support audit alongside continued native backend development;
do not postpone every installation task to M11. M11 verifies the complete delivery.
