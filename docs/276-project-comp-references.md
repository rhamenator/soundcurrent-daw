# Trusted comp selections in project state

Checkpoint 276 adds portable project-owned references for durable comp recovery.
The session stores each operation UUID and its selection SHA-256. These are
accepted owner values, not hashes discovered from a job's own sidecar. They do
not authorize playback, processing-quality admission or final model adoption.

## Versioning and validation

Projects containing references use schema 1.21 and a strict `compPairSelections`
array. Each entry has exactly `operation` and `selectionSha256`. UUIDs share the
existing global identity namespace; duplicates and collisions with audio objects
are refused. Digests require 64 lowercase hexadecimal characters. Existing
encoded/parser/canonical/state budgets cover the collection and validation work;
there is no new fixed product count limit.

Schema 1.21 retains the 1.20 clip representation, including explicit nullable
handle windows. Projects without references continue using their existing 1.18,
1.19 or 1.20 representation. Older projects migrate with an empty reference
collection; unknown schemas and fields remain explicit refusals.

Project load does not require the comp sidecars to exist. A missing, corrupt,
stale or interrupted job is a recovery condition, not a reason to lose the main
project. The existing audio-asset and native-journal verification remains intact.
The project file is the owner authority under the existing trusted project
boundary; this is integrity binding, not cryptographic authorship authentication.

## Registration, history and reopening

Publish the immutable recipe, child requests and selection first. Derive a
reference from the accepted selection and current context, then submit
`AddCompPairSelection` and save the project. Identical registration is idempotent;
a conflicting digest for the same operation is refused. Removing a reference
only edits the model. It does not delete job data or terminate a process.

Undo/Redo retains both reference collections under history budgets. Conflicting
current references refuse an Undo without partial mutation. Reference changes can
participate in the same atomic structural transaction as later comp adoption;
this checkpoint does not yet implement that adoption path.

`loadReferencedCompPairSelection` finds the operation in the current project,
uses its saved digest to reopen the selection, and rederives all recipe/requests
with a fresh context. It cannot substitute a self-reported sidecar digest when
there is no project reference. Existing completion/source/PCM verification still
runs before a recovered candidate becomes ready. Absolute roots and runtime
epochs are not restored from saved references.

## Acceptance and remaining work

[Source-bound acceptance](../tests/results/M2/2026-10-14-comp-pair-state/index.json)
binds implementation `8e44da455b817176b2f2ba6148bd0be0f257dadc`, source/library/executable hashes
and CTest process 3854791. All six focused suites pass in 5.75 seconds:
project-reference state, paired worker recovery, ordinary session state, existing
comp-window state, history resources and multitrack edits. Four actual workers
produce two completed spans plus cancellation and watchdog retirement. Reopening
the relocated completed pair uses the actual saved project reference and a fresh
root/context. This is integrity/recovery acceptance, not DSP quality admission.

The frozen archive includes actual projects/previous generations, worker/media/
selection/control/completion files, relocation copies and source/process receipts:
4,120,777 bytes, 148 entries,
SHA-256 `e28666b4fa96a2aae03d0adcb704806b23b42b10da8a0aeea8aaf55d622fe5d8`. The dedicated Release build was incrementally
rebuilt for the changed session ABI with one low-priority worker. Existing GCC
mix-reader/playback warnings remain recorded. Localization extraction remains
1,038 keys/34 partial catalogs/3,168 drafts/zero reviewed or fully UI-qualified
languages. No production GUI source changed.

Original previous [CI 38611054401](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38611054401)
at `1b2c651` is still running at this checkpoint. Its exact original terminal
results must be retained before advancing the PR head. This project's new native
Windows reference gate is pending; prior worker evidence does not qualify it.


The acceptance receipt records strict state roundtrips, registration/removal
Undo/Redo, conflicts, malformed/colliding entries, count/resource admission,
previous generations, interrupted publication and missing sidecars. The paired
worker fixture now registers and saves references in the actual project rather
than using an external owner-reference file. It separately checks reopening the
completed pair after relocation with a fresh root/context.

Desktop scheduling still needs a process supervisor with memory reservations,
spawning/reaping, start/cancel/close barriers and live epoch/root ownership. A saved
reference is not a process handle. Actual mid-render crashes/power-loss, native
Windows reference execution, independent curve/alignment/scoped final PCM,
physical/post-SRC assembly, nonuniform/protected production and processing quality
remain required. No GUI/audio-route/installed-preview/equalizer/VM changes or new
dependencies are part of this increment. Full frozen-reference and European
localization scope remains active.
