# Native Windows desktop import/localization gate

Date: 2026-10-09 UTC. Frozen product baseline unchanged.

PR63 merged at `3a02b573e3290953060288ded9dc6fca75843057`. Corrected exact head
`387022d982728756aeb53853ec95bd7796353f43` passes78 hosted Linux tests,15 selected
MSVC tests and the Windows cross-build. The native worker/parent boundary passes
569 checks on the seven original Linux-writer projects, seven malformed/ambiguous
cases and28 corrupted reports.890 original source-property checks also pass.
Exact corrected metadata, archives and logs are retained in
`tests/results/X004/2026-10-09-property-preview-hosted`. The earlier UTF-8-reader
failure and unexplained manual-recording refusal remain retained separately.
The passing later run is not an explanation of the original recording failure.

## Remaining platform gap

The successful native gate does not build or execute Qt. New Properties/Original
source tabs, saved inspections, older-v1 compatibility, literal foreign text,
summary counts, session preservation and asynchronous widget/worker retirement
therefore need native Windows desktop tests. Source-property preservation still
is not audio/project conversion. Neither platform has native/render compatibility
qualification for these projects.

## Selected route

Add a separate hosted Windows2025/MSVC Release job for the actual Qt tests, using
our existing QtBase6.12.0 SDK and libsndfile1.2.2 source. This avoids a long local
VM build and leaves the three existing required contexts unchanged. The new job
will become a required protection context after its actual workflow passes; no
unqualified new gate is used to weaken or bypass the current protection.

The [official Qt6.12 MSVC package index](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/)
identifies the fixed package version `6.12.0-0-202609280346`. We download only
QtBase,40,184,745 bytes, SHA-256
`219d1b86def3be418e31d704572bc4acc91b2ec373c65595c9fb12b42bf098dc`.
A fresh official download matches the retained independent SDK archive. No
QtCreator, QML/add-on module or package-manager dependency is selected.

`research/windows-desktop-test-dependencies.json` pins the archive URL/size/hash,
source anchor, Core/Gui/Widgets/Test DLLs and offscreen platform plugin. The first
three product DLL identities and source anchor must match the separate existing
product dependency record. The original preparation script verifies identities
before use, extracts only the pinned archive into an owned CI directory, and
fails instead of substituting a different version. No test SDK is copied to the
repository or distributed product.

The job builds the real main target, import controller/UI tests, localization
tests and their actual worker. `QT_QPA_PLATFORM=offscreen` and the pinned plugin/
runtime paths exercise native Windows Qt code without an audio endpoint or an
interactive desktop. Tests execute actual child processes, Save/Open, Unicode
paths/names, missing/invalid values, read-only tables, current-session preservation,
old-format reopen and async close/retirement. Existing locale fixtures additionally
exercise draft catalog loads, number formatting, settings/Save/reopen and RTL
charts; their actual native results remain to be inspected. A catalog load does
not establish native-speaker review, full-European coverage or UI qualification.

## Licensing and scope

QtBase Core/Gui/Widgets remain the existing LGPL3/GPL3-compatible GUI dependency;
QtTest and offscreen code are test-only under their supplied terms. Existing
release source/notices/SBOM duties are unchanged. The LGPL libsndfile build uses
its existing pinned source and external codecs remain disabled. PowerShell,
7-Zip, MSVC/CMake/Ninja are existing hosted development tools, not new product
runtime dependencies. No code/assets from proprietary DAW suites or equalizer
changes are introduced. [ADR087](decisions/087-hosted-native-desktop-tests.md)
records the route and maintenance cost.

Hosted native offscreen evidence will qualify the tested Windows Qt workflows,
not interactive accessibility, physical/sustained audio, OS memory sandboxing,
installed binaries, upgrades/removal, arbitrary host/driver behavior, source-writer
Windows or semantic/render equivalence. Only logs and verified runtime metadata
are retained by CI; no executable/DLL/product release is uploaded.

Local preparation verifies the official archive against the existing SDK,
PowerShell syntax and workflow parsing. Native acquisition and the application/test build succeeded in the first hosted
run; corrected controller execution remains pending. No local VM, user audio route,
equalizer checkout or installed preview changed.

Next: qualify the actual new hosted UI workflow and retain its exact evidence;
then approved media roots/missing choices and opt-in new-project conversion with
Undo/reopen and independently aligned renders. Refresh local installer/source
pairs only after their concrete scope gates. All four full-parity axes remain
active/incomplete.

## First actual native run and test-fixture correction

Exact `df2065310350bcf60c20c569312dec13811f7d5f`, run37882708133,
job113665771573 builds the actual application and tests with the verified SDK.
UI passes90 checks and localization passes248 checks/34 draft catalog loads.
The controller fails `Malformed/deep/duplicate protocol accepted`; original
metadata, digest-verified artifact, runtime record and log are retained under
`tests/results/X004/2026-10-09-windows-desktop-native`.

The old truncation test erases the final two response bytes. The worker writes a
final newline through its text-mode standard stream; native Windows uses CRLF.
Removing CRLF leaves valid JSON. A controlled Linux reproduction forces CRLF on
the real child's response, applies that exact old mutation and reproduces the
failure. The fixture now removes the final object brace, independently confirms
malformed JSON and exercises actual stdout plus explicit LF/CRLF endings. It also
identifies each fault in refusal diagnostics. Product schema/decoder, worker,
quotas, deadlines and acceptance policy are unchanged.

Local Release passes all three selected desktop tests (77 controller,90 UI,
259 localization checks); ASan/UBSan passes77 controller checks with leak detection
disabled. Corrected native execution is pending; the failing new context remains
unpromoted and this PR must not merge until it passes. The previous three jobs
pass at the original head. No local VM or installed preview changed.
