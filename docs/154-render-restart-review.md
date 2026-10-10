# Retained render review after restarting

Status: implementation checkpoint; full product goal remains incomplete.

Open a saved project and choose **Edit → Review retained pitch/stretch renders…**.
The background scan shows ready, already attached, stale, incomplete, active and
invalid jobs. Select a ready job and choose **Review selected result**. Its exact
duration, pitch, formants, explicit context and protected markers reopen in the
clip dialog. Prepare audition and Apply retain their existing explicit controls.
Applying remains one transactional Undo/Redo edit; save and WAV export use the
shared project state and processing graph.

## Persistent ownership and identity

Before spawning the helper, the parent publishes an immutable
`media/stretch-selections/<operation>.json` selection. The helper still exclusively
creates `media/derived/<operation>`; the parent does not precreate that directory.
The separate `sc-stretch-selection-v1` document contains:

- Operation ID and expected render key.
- A minimal versioned project snapshot: original project ID/rate, one target
  track/clip, and its one or two referenced original/previously rendered assets.
- The complete explicit request, including its exact rational multiplier. This
  matters when different ratios round to the same whole-source output duration.
- The original bounded helper policy, checked against the current trusted ceiling.

The project codec supplies migrations and typed clip/anchor state. Unrelated
routes, names, EQ settings, transport, master and import state are excluded.
The retained EQ processor ID is stable; regenerating it would invalidate every
otherwise equivalent decoded minimal snapshot.

Publication uses exclusive temporary creation, file flush, no-overwrite
`publishMedia`, and directory flush where supported. A namespace lease serializes
publication and inventory. Recording journals retain their existing replacement
semantics and 16-KiB limit. Interrupted `.partial` files are counted and preserved.
New stretch helpers hold `writer.lock` from intent preparation through exit.
This preserves existing v4/v5 requests/completions and enables active-job refusal.

## Admission, verification and stale targets

JSON size/depth, duplicate fields, exact field shape, integer/boolean types,
IDs, canonical settings, minimal snapshot shape, and reconstructed request/key
must match. Approved directory/file handles bind reads, deny links/reparse points
and unexpected file types, and detect changes to inspected files. A held helper
lease classifies the job as active. An absent completion is incomplete, regardless
of PID or elapsed time.

Completed jobs are independently verified with the existing raw-source hash,
RF64 structure/format, whole sample hash, file hash, frame count and float peak
checks. The current project ID/rate, target clip and original asset/anchor must
still match. Registered operation IDs cannot name different media. Correctly
referenced completed derivatives classify as attached; Undo can legitimately
make them ready again. Unrelated track names and EQ edits do not invalidate them.

Review repeats verification rather than trusting an earlier scan. It binds the
result to the current immutable project opening. Apply still requires the current
epoch/root/project identity and the backend's full expected-clip equality guard.
No scan attaches media, restarts a helper, deletes jobs or changes the Session.
Cancellation is cooperative around bounded OS reads and verification chunks.
These controls are not a privilege sandbox or authentication of files edited by
the local account.

## Threading and resources

Codec, enumeration, file I/O and verification run on the stretch control worker;
audio callbacks and the GUI do not perform them. Retained plans/results and the
bounded GUI list have ledger credit. Default preview admission permits 128 stored
selection entries, 512 KiB per selection and 8 MiB total, with 16 MiB codec work
credit and an 8-MiB project state/parser ceiling. A resource/count refusal does
not launch another child or overwrite a selection. The 128-entry preview limit
is not a completed scalable lifetime job-management policy: pagination and
explicit archival remain required. Legacy jobs without parent selections are
not reconstructed by inference.

## Acceptance and remaining gates

`tests/stretch_controller_tests.cpp` covers a destroyed/reconstructed controller,
independent review, attached/reopened/Undo classification, stale and missing
targets, unchanged unrelated controls, helper ownership, immutable publication,
malformed types/fields/depth/duplicate keys, changed raw/rendered/completion files,
link refusal on Linux, Unicode, count/byte/ledger/cancellation bounds, protected
marker preservation and altered-marker refusal.

`tests/stretch_ui_tests.cpp` creates a new window after closing an unattached
render, scans and explicitly reviews it, checks unchanged/changed/restored
controls, Apply/Undo/Redo, save/reopen/export, and refuses repeat attachment in a
third window. It uses actual helper processes and synthetic owned audio; it does
not open native endpoints. These tests are included in the existing Linux and
native Windows desktop CI gates.

Installed Linux/Windows restart workflows, abrupt parent termination,
interrupted publication durability, Windows reparse/held-writer qualification,
large-inventory management and native-speaker review of the new strings remain
separate gates. Updated catalogs include English source text and unfinished
entries for other languages; they do not establish all-Europe localization.
The previous installed Windows checkpoint remains bound to its original source,
not this changed implementation. No refreshed installer is qualified by these
synthetic tests.

Next: qualify the actual installed restart/review workflow on Linux and Windows,
then continue M2 processing-quality and scalable recovery work. Full M2–M11,
X004/X005, functional/quality/content/native-compatibility and European language
requirements remain active.
