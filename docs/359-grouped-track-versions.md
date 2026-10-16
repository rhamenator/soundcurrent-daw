# Grouped audio track versions

Checkpoint 359 extends the audio arrangement model with shared version groups and
a desktop workflow for changing multiple tracks together. P010 remains partial:
other track types, automation interaction and interchange still require work.

## Workflow

Open **Track versions…** on a stopped audio track. The left list shows that track's
versions; **Tracks to change** shows each track and its active version. Click to
select additional tracks; the current track remains selected. The list supports
keyboard navigation and scrolling. The selected count uses locale formatting.

- **New empty** and **Duplicate active** create a shared version across selected
  tracks. Previously implicit original arrangements also receive a common group
  when first named together. Existing named groups keep their identities.
- **Select matching tracks** selects tracks that contain the highlighted version's
  shared group, regardless of version names or whether that version is active.
- **Activate selected**, **Rename selected**, and **Remove selected** operate on
  that group in the selected tracks. Every selected track must have a matching
  version. Removal requires that version to be inactive on every selected track.
- **Link active versions** assigns a new shared group to the selected tracks'
  current arrangements, allowing independently created arrangements to be linked.
  It leaves unselected peers and audio content unchanged.

Each operation is one Undo/Redo transaction. Missing tracks, duplicate targets,
missing group members, active-version removal, unknown operations, invalid state
or insufficient resources reject the entire transaction. The UI disables
incompatible operations and explains unmatched selections. Cancel, stale project
snapshots and busy transport retain the earlier protection.

## Identity and state

Each version retains its unique object ID and can additionally carry a shared
`groupId`. A shared ID can occur at most once on any one track. It may appear on
multiple tracks, but cannot collide with an object or recorded-take group ID.
Names are independent of matching, and take groups remain a separate concept.

Multi-track commands carry an explicit track-ID selection. A bounded preparation
pass rejects duplicates and missing tracks, then visits tracks in project order.
The command operates on a candidate session and validates it before publication.
It supports more than the existing 64-operation batch bound without converting a
group into a partially applied series of transactions. State, history, creation,
selection and group-edit staging include nested payload and group identity costs.

Only active clips enter playback and offline export. Linking groups changes no
samples, clip timing, processing, routing or retained takes. The dialog borrows
immutable snapshots, generates labels on demand, and reserves a shared GUI budget
for track selection and temporary projections before opening.

Schema **1.25** carries nullable `groupId` fields on named version identities when
any version is grouped. Ungrouped version projects retain 1.24 encoding. Earlier
projects acquire no fabricated group on load. Older readers reject grouped
projects rather than dropping their links. Undo and reopen preserve exact IDs.
No dependency, audio callback, external driver or equalizer repository changes.

## Frozen reference mapping

The frozen [Reference B 15 version identity topic](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/track_handling/trackhandling_trackversion_ids_c.html?contentId=i0eoH157rAAe6qXZHpZrAg)
describes grouping versions created together. Its [common identity workflow](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/track_handling/trackhandling_assigning_a_common_id_t.html?contentId=UExFuJFVfZqG8fUIHMATHA)
links independently created active arrangements across selected tracks. These
semantics inform this original model and UI; proprietary implementation code or
assets were not used. They do not establish other P010 workflows or quality parity.

## CI failure and evidence

The preceding run `38829564391` on `53d9a9b` completed with all Windows jobs passing.
Two Linux shards failed the catalog structural checks because UI file hashes in
`localization/source-inventory.json` were stale after waveform changes. The Linux
aggregate gate therefore also failed. Regenerating catalogs and inventory is the
fix; the hash checks remain strict. The current source is independently tested.

All 15 original terminal job logs and all 12 original artifact ZIPs were retained
before advancing the PR. Original artifact sizes, API SHA-256 digests and ZIP CRCs
were verified. The failed run remains recorded as failed; it was not canceled or
replaced with a success claim.

Source: `0f6c7f6efad74dbb2c56da89e8ee904ac6ce13fa`, based on
`5bd241765971dac317580a98852500372b49f09c`.

- Final Release: all six suites pass in 6.91 seconds — track-version desktop,
  session state, retained-take/comp, history resources, catalog structural audit
  and catalog rejection tests. Both checks from the preceding CI failure pass.
- ASan/UBSan, leak detection and halt-on-error enabled: all three selected suites
  pass in 4.46 seconds — track-version desktop, session state and retained-take/comp.
- The take/comp suite completes 14,272 checks with zero guarded audio-callback
  allocations, frees or blocking locks. It verifies shared creation, sample-exact
  group activation/live/export, selected-scope relinking, collision/refusal cases,
  late missing-peer rollback, resource admission and exact Undo/Redo/reopen. A
  129-track group verifies every target on creation and activation, then restores
  both transactions exactly through Undo.
- Desktop: 137 Release checks including the captured dialog; 136 sanitizer checks.
  Matching-track selection, shared creation/rename/switch/removal, relinking an
  independently named track, unselected-peer preservation and unmatched selection
  refusal pass. The 680×680 offscreen capture was visually reviewed. Native desktop
  and installed-preview qualification remain separate.
- History resources: 829 checks; session state: 135 checks. Localization audit:
  34 catalogs, 1,131 source keys, 3,168 draft translations; no language is thereby
  native-reviewed or fully UI-qualified. Project-controlled tooltip names are
  escaped before Qt rich-text rendering.

[Acceptance archive](../tests/results/M2/2026-10-15-grouped-track-versions/acceptance.zip):
110 entries, 1,573,276 bytes, SHA-256
`2bad1127081efdefb63cb98d8ac81834a48f64027757eae8bd3a36c814e6551c`.
It binds the exact changed source, diff, original local build/test logs, command
lines, executable hashes, catalog audit and dialog capture. It also includes all
15 preceding CI logs and metadata identifying the 12 original artifact ZIPs;
those ZIPs remain retained locally. Archive CRC and entry hashes were verified.
No executables, credentials or VM media are included.

Initial passing logs precede the final 129-track and rollback assertions and the
tooltip escaping change; the final Release and sanitizer logs qualify this source.
Installed previews have not changed. Fresh Linux/Windows CI and preview packaging
must qualify this exact revision before it is presented as an updated preview.
