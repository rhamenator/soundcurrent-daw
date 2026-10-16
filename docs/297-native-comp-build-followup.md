# Native comp path handling and shared desktop compilation

Checkpoint 297, 2026-10-14. The complete DAW goal remains active. The installed
preview is unchanged.

## Original CI findings

[Original run 38664035143](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38664035143)
at `42813e436767409d6e976e35f5cfbe03392b025e` ended **cancelled**, with:

| Job | Original result |
| --- | --- |
| Linux synthetic shard 0 | 97/97 passed |
| Linux synthetic shard 1 | 96/96 passed |
| Linux required aggregate | Passed |
| Windows core cross-build | Passed; no native runtime claim |
| Windows native workers | 17/20 passed in the comp group; three relocation failures |
| Windows desktop | 26/26 tests passed in 152.10 seconds; overall job cancelled at its 20-minute budget |

The two Linux assignments cover exactly 193 distinct tests without overlap.
All six terminal job APIs and full logs, all six original artifact ZIPs and all
26 PNGs were retained and verified before another PR head advance. Artifact
lengths, SHA-256 digests and ZIP CRCs agree with GitHub's records. The Windows
bundle audit verifies 1,703 corresponding-source files against the actual Windows
Git checkout filters, all 15 deployment files and both media dependency builds.
This establishes artifact provenance, not installer or physical-audio qualification.
Representative native Windows creation and Linux applied-timeline captures were
visually inspected.

## Corrections

The three native failures share a recursive project copy in the test fixture.
They now pass source and destination through the existing Windows extended-path
adapter. The repeated desktop fixture uses the same adapter. A Unicode project
longer than 320 native path units additionally checks relocation of current and
previous project state, media, journal and preservation of the source project.
Native execution of these corrections still requires fresh CI.

The original desktop job compiled identical `ui/studio_window.cpp` code eleven
times. [ADR 186](decisions/186-shared-desktop-window-build.md) moves it into a
shared static target while preserving executable-specific test code, audit hooks,
link options, warnings and Qt usage requirements. The generated local build graph
contains one window compilation. No processing algorithm, control, localization
string, test selection or existing deadline changed. Actual new native job duration
must be measured; the earlier cancelled job is not reported as green.

## Local qualification and retained failure

The first source-bound Release run at `8616620` passed 19/20 selected suites in
497.62 seconds. Its new relocation assertion failed because the injected failed
save had already refreshed the previous-project backup. Source and copy contained
identical state; the assertion incorrectly expected the older backup. The failed
project and original logs are retained. Commit `68e6139` compares against the backup
present immediately before copying and also verifies the source backup is unchanged.

Three history suites in the initial run used stale helpers and are diagnostic
only. After rebuilding all affected helpers, a Ninja dry run reported no pending
work. A new source-bound run at `68e61394a7ab6b129b1e6d778ed1b8e62926af59` passed all
four affected suites in 122.73 seconds: native-media-long-path, source regeneration,
history regeneration and paired history review. The original sixteen other passed
suites used rebuilt executables. Together these provide current-build coverage of
all twenty selected suites; they are not described as one clean twenty-test run.

Coverage includes actual repeated desktop creation/review/Apply, first-generation
creation and Apply, stretch/protected editing, import, localization, recording
panel, retained takes, timeline, loop controls, session state and partition checks.
The repeated desktop suite passed in 233.90 seconds. Source and executable hashes
remained stable during each qualification. Earlier MinGW syntax checks passed for
the two path fixture translation units; that is not native Windows execution.

## Debug finding and next implementation gate

A source-bound 44.1 kHz Debug probe at `68e6139` failed after 67.21 seconds with
`Transition UI observation deadline`. It completed the first repeated creation,
Review/Apply, Undo/Redo, save/reopen and audio checks. The second creation published
both span completions and an assembly completion; cancellation during teardown
followed the observation failure. These retained file timestamps narrow the slow
path to cumulative rendering/publication/review work, but do not identify a CPU
hotspot or prove when the UI observed a particular phase.

The second-generation audio was not adopted. The original failed project,
progress report, captures, stdout/stderr and exact source/binary hashes are retained.
No test or product deadline was increased. Debug repeated-desktop qualification
remains **failed**, and the PR head is held pending a measured timing correction.
Next: instrument bounded stage timing/profile historical verification in this
owned Debug workflow, remove demonstrated redundant work while preserving fresh
file validation, then rerun the affected workflow and regression checks.

[Local evidence](../tests/results/M2/2026-10-14-native-comp-followup/local-index.json)
includes both Release runs and this failed Debug probe, with original corpus bytes.
[Original CI review](../tests/results/M2/2026-10-14-native-comp-followup/ci-index.json)
retains full logs, API records, audit tools and all PNGs; its index records the six
original ZIP digests. Those original ZIPs are also retained locally.

Fresh native Windows CI, installed preview delivery, processing-quality gates,
nonuniform/protected comp production, full M2–M11, equipment/import coverage and
reviewed European translations remain incomplete. No broader parity is claimed.
