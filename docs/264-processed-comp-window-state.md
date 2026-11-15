# Processed comp handle window state

Checkpoint 264 separates the nominal stretch artifact from a processed playback
window with explicit prefix/suffix handles. It implements project state, exact
mapping and edit preservation. It does not yet generate audio or expose worker
admission through the UI.

## Project format and provenance

The live repository supported project formats through 1.19 before this change;
the earlier recovery note's 1.15 referred to the stretch/warp format, not the whole
project. Projects containing `compHandles` now use schema 1.20. Ordinary projects
keep their existing 1.18/1.19 encoding; older projects decode with no invented
handle window. Unknown keys, versions and processors are refused.

`ClipCompHandleWindow` records the original nominal asset ID/hash, prefix/suffix
frames at the physical artifact rate, and the extended artifact's render key.
The existing stretch anchor retains its original raw asset/hash, fractional
origin, nominal source span, settings, processor, context, warp markers and key.
The selected clip addresses the extended artifact; the original nominal asset
remains an explicit project dependency. Zero-prefix/suffix context exposure can
reuse the existing verified nominal artifact and key.

State validation checks nominal and extended identities, rate/layout, bounded
buffer geometry, actual last-sample raw coverage, and the selected fragment's
retained-take coverage. A fractional exclusive endpoint can exceed a physical
asset boundary while all actual samples remain inside it. That endpoint must
not be treated as an additional sample. This is geometry validation against
asset declarations, not proof of generated WAV bytes or processor quality.

Exact lookup reconstructs context or protected-warp maps and extends the first
or last interval as required. The handle planner accepts existing processed
windows, preserves their original nominal dependency, and rejects stale nominal
asset declarations. Session snapshots and Undo payloads account for the new
owned strings. These operations allocate and acquire resource credit on control;
they never belong in real-time callbacks.

## Edits and remaining implementation

Split and paired transitions preserve both nominal and handle provenance.
Undo/Redo and save/reopen retain it. Ordinary stretch preparation/adoption now
explicitly refuses processed windows pending paired regeneration; it cannot
silently replace or discard their anchors. Supporting that regeneration remains
required, alongside actual existing-worker generation, artifact review, atomic
source admission, cancellation, controller revision/root binding and migrations.
The ordinary `ApplyClipStretch` raw-anchor equality guard remains intact.

The next worker increment must generate actual missing audio, preserve original
nominal PCM, distinguish fractional map endpoints from integer processing grids,
and verify whole PCM and SRC/warp/context edge quality independently. No metadata
fixture or copied integer base samples establish that quality. New UI/preview
workflow delivery follows actual generation and admission.

## Evidence

[Source-bound acceptance](../tests/results/M2/2026-10-13-processed-comp-window-state/index.json)
records source `5244ae19f138f4ee6fb478433d0fdf92e225a1ee`, unchanged inputs,
executable hashes and the actual test process. A fresh single-worker Release build
passes nine selected gates in 1.58 seconds, including 75 window-state checks,
156 planner checks, 6,807 existing comp-transition checks, legacy state/resource
checks and independent timing/geometry probes. The model fixtures use declared
assets and fake hashes; they perform no audio render. A separate compiled disk
experiment writes owned silent WAV fixtures with actual hashes, then verifies
three actual project save/reopen cases and six previous-generation recoveries on
Unicode paths. The initial missing-media and mismatched-project-ID fixture
failures are retained; both demonstrate existing store safeguards. This experiment
qualifies persistence, not DSP processing or rendered-handle quality.

The preceding planner source `7bf5140` passed all four original hosted jobs in
[run 38485663885](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38485663885):
91 native Windows, 22 desktop Windows and 166 Linux suites. Its original five
ZIPs, APIs, logs and PNGs are retained. Independent byte/source review verifies
14 deployment files, 1,516 corresponding source files and both reviewed media
DLL builds. Main window, physical audio and installer qualification flags remain
false. The earlier Windows recording delay remains unexplained.

This window-state source is under [run 38488371125](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38488371125)
at this checkpoint. Preserve its original terminal result before branch updates.
Installed current/previous Ubuntu previews and equalizer repositories are unchanged.
Full-suite, Windows delivery and European language qualification remain open.
