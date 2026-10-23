# Persistent comp playback and paired adoption

Checkpoint 285, 2026-10-14. The full frozen professional DAW goal remains active.

## Implemented workflow

Assembled audio now has canonical project state. Conditional schema 1.23 stores
the actual playback asset and geometry alongside the original clip, raw/base/
nominal asset declarations, retained take identity and exact rational raw timing
map. Projects without these records continue using their existing schema.
Asset references are bound by identity and SHA-256; the codec reconstructs their
declarations from the project's asset registry. Workspace paths, live owner
epochs and executable locations are not serialized into these records.

The persistent record distinguishes physical-rate artifacts from audio already
converted to the project rate. Playback of the latter uses the actual artifact
at unit speed, avoiding a second application of the original conversion. The
historical project rate and processing anchors remain available independently
of later timeline edits. Historical clip snapshots do not create duplicate
active clip identities.

Validation checks paired request/selection bindings, output ownership, asset
declarations, geometry, exact raw-map derivation, take provenance and acyclic
source lineage. Adopted operation IDs remain reserved; their output IDs become
ordinary registered asset identities. Memory accounting includes the retained
snapshots, maps and validation staging. Malformed, partial or inconsistent
records fail instead of silently losing their original source identity.

`ApplyCompAssembly` consumes one pending request, registers generated assets and
their provenance, replaces the affected fragments and prepares their transition
as one atomic edit. Both current fragments, retained takes, source assets and
independent selection/request references must still match. Existing-media sides
can expose valid nominal context without creating a new file. Undo restores
the original model and reservations; Redo restores the complete adopted model.
Neither operation deletes source or output files. History accounting and
conflict detection include the retained provenance.

This model edit is not a filesystem authority token. Its caller must independently
verify the owned output files and current workspace/epoch before applying it.
The actual acceptance workflow does this through `verifyCompAssembly`. The
main window does not yet expose the new Apply workflow.

## Qualified evidence

Source `c98af977dedcdace1dc07820312a78d6d8a0c5c9` passes six source-bound Linux
suites. CTest process 3979575 exited successfully; test time was 33.06 seconds
and enclosing qualification 33.07 seconds. Source, executable and local static
library hashes were unchanged throughout the run. The
[evidence index](../tests/results/M2/2026-10-14-comp-playback-state/index.json)
indexes the complete 620-entry, 17,214,672-byte acceptance archive.

- The processing corpus covers 110 prepared sources, 54 persistent pairs and
  95,098 assertions. It retains the earlier 51 physical and 35 post-SRC PCM cases.
- Metadata cases exercise codec round trips, stale source/selection refusal,
  partial-pair rejection, malformed maps/identities, history conflicts and
  state/history budget refusals. Crop, split and playback-rate edits preserve
  the original provenance.
- Three actual audio workflows use 44.1, 48 and 96 kHz source fixtures. Each
  verifies the generated assembly, adopts it, performs Undo/Redo, saves and
  reopens the project, plays it through `TrackReader`, `PlaybackPipe` and
  `PlaybackProcessor`, and exports WAV through the existing offline path.
- All 202,404 interleaved playback samples exactly match decoded WAV output.
  Playback callbacks use 193 frames; export uses 127-frame blocks. Floating
  peaks of approximately 4.117, 3.637 and 3.851 remain unclipped internally.
  These intentionally high test levels are not physical listening levels.
- The original publication/failure corpus still passes: twelve nominal/span
  workers, three publication processes, three completed jobs, six interrupted
  jobs and three canceled jobs. Full independent assembly comparison covers
  202,716 samples; counterfeit output with repaired hashes is still refused.
- Playback/export leave the saved project unchanged. Undo afterward restores
  the original pending request, and that state saves/reopens successfully.

The stable build completed after all API changes. GCC emitted existing
optional-resource/shared-pointer diagnostics and an unused variable warning in
the existing mix reader; this is not a warning-free build claim.

## Preceding original CI

Original run [38630299802](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38630299802)
at `b38969a91b91c1b88ff2efcf74f4f14d5b18dfdd` completed all four jobs successfully.
Its terminal APIs, complete job logs, all five original ZIPs and both PNGs were
retained before updating the PR head. The
[review index](../tests/results/M2/2026-10-14-comp-playback-state/pr194-controller-ci/index.json)
records original artifact hashes and the compact public evidence archive.

The original Linux and Windows controller corpora each pass 15 cases and 171
checks. All controller processes and twelve source workers per platform retire;
controller close releases memory and GUI timers advance at publication barriers.
Independent byte inspection verifies sixteen completed output WAVs across both
platforms. The Windows preview audit verifies fourteen payloads and 1,633 source
files against the actual Windows checkout filters and pinned media dependencies.
This qualifies the preceding controller revision, not Windows execution of the
new schema 1.23/adoption code.

## Remaining integration

Next connect independently verified completion to main-window Apply with fresh
owner/root/epoch checks, asynchronous ownership, paired history and project save.
Keep the existing processing-quality admission gates; passing persistence tests
does not admit a new renderer for general use.

Source-aware repeated comp/stretch regeneration remains required. The current
stretch preparation explicitly refuses an assembled asset instead of treating
it as fresh raw audio. The persistent map supports lineage, but that alone is
not a repeated-production implementation. Nonuniform/protected production,
spectral spur findings, mixed joins, phase/formant/transient and listening
quality remain open. These tests use the live engine callback path without a
physical endpoint; native playback, installation and a user-facing preview of
this workflow remain unqualified. Full M2–M11, F/Q/C/N, X004/X005 and all-Europe
localization requirements remain active.
