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
