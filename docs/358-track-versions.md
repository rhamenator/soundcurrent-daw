# Audio track versions

Checkpoint 358 adds alternate audio arrangements to P010. A track can create an
empty version or duplicate its active arrangement, switch to a retained version,
rename a version, and remove an inactive version. Each operation uses the existing
structural edit transaction and Undo/Redo history. This is an audio-version
foundation; full P010 functional parity is still open.

## User workflow

Stop transport, select an audio track, and open **Track versions…** in the timeline.
The first list row is the active arrangement; its name is also displayed above the
list. Enter a name for **New empty** or **Duplicate active**. Select another row and
choose **Activate selected**, **Rename selected**, or **Remove selected**. The
currently active arrangement cannot be removed: explicitly activate another first.
Cancel makes no change. The list scrolls and has keyboard selection. A changed
project or opening invalidates a dialog before it can submit an edit.

The dialog borrows an immutable project snapshot and reads names on demand. It
creates no widget, copied name, or lookup index per version. UI strings are
extracted into the existing 34 catalogs; untranslated entries remain unfinished.
Catalog presence is not native-speaker review or completed European coverage.

## Model, media and execution

`Track::clips` remains the only audible arrangement. Optional `activeVersion`
metadata names it; `inactiveVersions` stores the other identities, names and clip
vectors. Switching swaps those vectors and identities in a control-side candidate
session. Duplicating allocates new clip identities, retaining exact processing,
source timing, stretch/comp provenance and references to shared raw takes. Empty
creation retains the old arrangement and activates no clips.

Routing, track EQ, monitoring, input latency, retained takes and their recording
group IDs remain shared track state. Inactive clips undergo the same extent,
layout, unique-ID, processing and retained-origin validation as active clips.
Missing raw or derived asset declarations and missing retained origins are
rejected even when the active version is empty. Assets remain project-owned and
are verified by the existing store; version removal does not delete files.

Version vectors, names and nested clip payloads count toward state and Undo
budgets. Creation preflights additional memory before duplicating clips, and the
completed candidate is validated before publication. There is no fixed small
version count limit; state budgets and the GUI row representation are explicit
bounds. Playback/offline readers continue consuming only active clips; no callback
performs version mutation, allocation, project I/O, or history work. The desktop
uses its existing stopped-transport edit gate. Seamless live version switching is
not implemented.

## Persistence

A project containing named versions writes schema **1.24**, with strict active
metadata and inactive clip arrays on every track. Earlier projects still load
without inventing named versions and keep their earlier compatible encoding until
versions are created. Older readers reject 1.24 rather than discarding alternate
arrangements. All clip fields and immutable comp playback state remain present.
Future schemas and unknown fields are rejected. Save/reopen and Undo/Redo preserve
identities rather than generating replacements during restoration.

## Baseline and remaining gaps

The frozen [Reference B 15 version workflow](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/track_handling/track_handling_trackversions_c.html?contentId=1gfDo1EXoVP~tQtYSUXjYw)
describes multiple event/part arrangements, comparisons of takes and comps,
multitrack takes, and persistence in archives/backups. It also covers other track
types and excludes automation tracks themselves from versioning. We preserve the
matrix requirement that shared automation survives arrangement changes; automation
is not yet implemented here, so that acceptance remains unqualified.

Still required: grouped-version identities and desktop selection/switching across
tracks, MIDI/instrument/sampler/video and global musical-track versions, completed
automation interaction, archive/import/export interchange, independent native
Windows qualification, and delivery in matching Linux/Windows previews. Core
multi-track batches can already switch atomically while preserving recorded take
groups, but that does not establish the full grouped desktop workflow.

## Acceptance evidence

Source: `fd5bfd60ce7992527366f3ed9406fee83dad0c4d`, based on
`53d9a9bec2dba21f991a6b80595b0ab94998d12c`.

- Release: all five suites pass in 3.51 seconds: session state, retained take/comp,
  history resources, track-version desktop, and comp recipe recovery.
- ASan/UBSan, leak detection and halt-on-error enabled: all four selected suites
  pass in 53.15 seconds: session state, retained take/comp, history resources and
  track-version desktop. No sanitizer findings.
- The expanded retained-take suite runs 13,938 checks with zero guarded callback
  allocations/frees/blocking locks. It compares active-only live/offline samples,
  preserves raw hashes, exercises empty/duplicated versions, exact Undo/Redo,
  eight-track atomic switches and recording-group preservation, 65 retained
  versions, schema refusal, missing inactive media/origins and resource refusal.
- UI: 88 Release checks (including the saved screenshot), 87 sanitizer checks.
  Unicode names, current-version action guards, blank names, stale opening,
  read-only editing, cancellation, Undo/Redo and save/reopen pass. The 500×580
  offscreen dialog capture was visually inspected; native display qualification
  is separate.
- Localization structural audit: 34 catalogs, 1,123 source keys, 3,168 draft
  translations; zero native-reviewed or fully UI-qualified languages.

[Acceptance archive](../tests/results/M2/2026-10-15-track-versions/acceptance.zip)
contains 64 entries: exact changed source, source diff, original build/test logs,
localization audit, dialog capture, commands, per-entry hashes and executable
hashes. It is 894,875 bytes, SHA-256
`daff663ac3821927f33b9732abc98c122a9539023317357356d9523679c4dc58`.
Archive CRC and every declared entry hash were verified. No executables,
credentials, VM images or physical-audio recordings are included.

The initial Release log precedes additional GUI guard assertions and the capture;
the final Release and sanitizer logs are authoritative. No failing run was hidden.
The preceding hosted run `38829564391` at `53d9a9b` is still active, so this source
checkpoint is backed up separately without canceling that run. New native Windows
CI coverage includes the version-dialog target and test. Installed Linux/Windows
previews remain unchanged; no installer or release was uploaded.
