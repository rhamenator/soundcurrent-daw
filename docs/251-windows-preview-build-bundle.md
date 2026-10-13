# Retaining current native Windows deployment inputs

Checkpoint 251, 2026-10-13. Full frozen-reference parity, hardware recording,
installers and all-Europe localization remain required. This adds collection
infrastructure; an updated Windows installer is not yet prepared or qualified.

## Gap and implementation

The existing hosted native desktop job builds the main application and helpers,
but its retained artifact contains logs rather than the complete deployment inputs
needed by `tools/package_windows_preview.py`. A newly marked optional stage now
retains those inputs on `codex/windows-preview*` PRs or manual workflow dispatch.
Other PRs retain the normal desktop test scope. No local VM or hardware route is
started by this stage.

- The native desktop checkout explicitly uses the requested head commit, rather
  than GitHub's generated merge commit. Other existing CI jobs remain unchanged.
- `tools/windows_preview_bundle.py` captures all tracked build/install inputs,
  excluding historical result media, before compilation. Collection requires the
  same source commit/tree, clean tracked state and every original byte hash afterward.
- The existing Release build also builds the stretch artifact/protected-marker
  verifiers. It runs the normal twenty desktop gates before collection.
- `.github/scripts/collect-windows-preview.ps1` uses the reviewed `windeployqt`
  options and copies the five actual built application/helper executables.
- A manifest and ZIP retain precisely five application files and nine runtime
  DLLs. Native x64 PE/PE32+ headers, membership, bounds, case collisions, links and
  Qt runtime hashes are checked. The application files must match the build bytes.
- A separate verification folder retains three actual verifier/panel executables,
  the hash-pinned QtTest DLL and offscreen plugin. These are test tools and are not
  part of the product install payload.
- Compiler file/version/hash, actual `dumpbin` imports, CMake cache, CTest log and
  SDK verification record accompany the bundle.
- Complete tracked application build/install sources (including vendored code,
  tests/fixtures, licenses and scripts) are archived, excluding historical result
  media. The full public source commit remains available. Exact pinned QtBase and
  libsndfile source archives accompany the runtime artifact. No build toolchain is
  uploaded; this is a temporary three-day CI input artifact, not a binary release.

## Two eligibility issues remain explicit

Hosted CI compiles libsndfile from its reviewed 1.2.2 source. That fresh DLL may
have a different identity from the separately qualified product DLL. Collection
records `productDependencyMismatches` and permits only that declared mismatch to
be retained for review. It does not modify the product pin, substitute arbitrary
Qt DLLs, or make the installer builder accept an unqualified dependency. The product
builder continues checking its independent exact dependency identities.

Original CI183 used MSVC 14.51.36231 from Visual Studio 18; the retained product
runtime is Microsoft 14.44.35211.0. This is a compiler/runtime qualification issue,
not proof that the old runtime will or will not load the new application. The
collector preserves actual compiler and import facts and always leaves runtime
compatibility unqualified. A new runtime must have an independent official-source,
signature/version and native compatibility review before its pin changes. Alternatively,
a reviewed existing toolchain/runtime pair can be used. No signing certificate
purchase is required to prepare the existing unsigned application installer.

Original CI worker receipts identify GitHub's synthetic merge commit. For CI183,
the actual worker commit was `b8bbc334af3cfef0e9fe7dfa11573b103ab0a013`, with tree
`b39cec6f15ffdce68f5283fdb593ad4bba10b305`, equal to application source5a20469's tree.
Thus the tests apply to those source bytes, but their literal commit-bound receipts
cannot be substituted into the package builder for commit5a20469. The new exact-head
checkout prevents this mismatch for future deployment collection. It does not rewrite
historical receipts or manufacture native process identities.

The collection explicitly leaves `mainWindowQualified`, `nativeAudioQualified`,
`installerPrepared` and `runtimeCompatibilityQualified` false. Collecting a bundle
cannot establish Windows endpoint recording, a normal interactive main-window close,
a helper process qualification bank or an installed workflow.

## Verification so far

On Linux, the audit read a copied actual retained x64 deployment. Its fourteen
files match the reviewed runtime identities. Changing the app to an x86 PE machine,
modifying the trusted Qt DLL, and adding an extra executable payload each refused.
A modified libsndfile DLL remained explicitly quarantined as the sole mismatch.
This is byte/structure validation, not native Windows execution of the new collector.
Python syntax, PowerShell AST and workflow structure also pass. Native collection
must be validated by its actual hosted run; parser success is not that proof.

Original [CI184 run38428513560](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38428513560)
for source11e643a completed all four jobs: Linux161/161 in395.31 seconds,
Windows native88/88 in261.71 seconds, desktop20/20 in64.72 seconds and cross-build.
[Original terminal APIs and all job logs](../tests/results/X007/2026-10-13-windows-build-input-preparation/index.json)
and original Windows artifact ZIPs are retained
under `.cache/windows-preview-bundle/pr184-review/`. Its native worker receipt has
merge commit `1e13fb55ac1bee26c0f250dec608998d9090fa0f`, tree
`fb13559f26ab72538bc427ef93a87cc0fb2600ce`; source11e643a has the same tree.
This original run predates the collection implementation and cannot qualify it.

## Next gate

Inspect the actual new hosted bundle and original collection output. Preserve any
failure before correction, and do not rerun unchanged source. Resolve the observed
libsndfile/runtime eligibility using independently reviewed binaries and actual native
main/helper receipts, then use the unchanged package builder for a matching setup/GPL
source pair. Only after offline preparation should brief local VM acceptance begin:
read the Copperfin policy, ensure no other VM is running and use a full independent
clone for installation or runtime changes. Preserve originals and previous preview
versions. The Linux preview and equalizer projects remain unchanged.
