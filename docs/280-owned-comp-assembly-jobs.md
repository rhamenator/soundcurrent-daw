# Owned final comp assembly jobs

Checkpoint 280 adds an owned disk/control transaction for final comp candidate
files, with immutable request/intent/completion records and read-only recovery.
The new library has no GUI or audio-callback work. Project registration of trusted
request digests, the desktop controller, final graph/model adoption and current
native Windows execution remain required.

## Request and publication

`CompAssemblyRequest` binds the current paired selection and recipe, a new job
UUID, explicit new output UUIDs, the actual assembler executable digest, combined
file-byte limits, PCM/deadline limits and original nominal policies. Canonical
request decoding requires an independently retained expected digest and rederives
its bytes from the current selection. Unknown/duplicate fields, wrong numeric
types, reused asset/job/output IDs, invalid grants and stale selection bindings are
refused. Full global project-ID validation will accompany project registration.

Publication checks current trusted pair references and the running executable,
creates a new owned `media/comp-assemblies/<job>` directory, acquires its writer
lease and writes immutable intent. It also takes an exclusive result-directory
lease. Each generated fragment streams through the existing verified nominal/
span/physical-or-post-SRC pipeline into a new floating RF64 file. Files are closed,
flushed and published without replacing an existing destination. Complete output
PCM is recomputed and checked before the final immutable completion is published.
The completion record is the paired commit; a first published file alone is never
an accepted pair. Existing jobs are not silently resumed, overwritten or deleted.

Work and retained result metadata use the caller's resource ledger, with bounded
slabs and conservative combined-file admission. Cancellation and deadlines are
cooperative. This library operation does not spawn a child or claim an OS memory
ceiling, a hard watchdog, exact allocator/RSS accounting or successful recovery
from physical power loss. The callback is an off-thread progress/interruption
boundary; immutable source files and serialized model snapshots remain required.
Final canonical adoption must revalidate the live owner after these sampled checks.

The caller must durably retain the original request digest independently of the
job's completion. Current tests keep that reference with their explicit owner
evidence. Registering it in versioned project state, and exposing recovery through
the desktop owner, are the next integration steps. A request read from an unknown
job must not authorize itself.

## Independent recovery checks

Recovery distinguishes absent, active, interrupted, canceled, complete and invalid
jobs. It preserves partial and published files. A valid committed completion
remains recoverable after a late cancellation request. Before returning a verified
result, the reader checks the original intent, completion shape and derived output
identity/geometry, full RF64 file hashes, decoded sample hashes and peaks.

It then independently recomputes every output sample from the original nominal
and span sources and compares float bits. A separate media-review pass also checks
unchanged physical and project scopes. Markers and current selection are checked
again afterward. This catches a modified sample even when its forged completion
contains consistent replacement hashes and the edit lies inside an otherwise
permitted blend. The recomputation uses the production assembler, while the
existing assembly corpus supplies its separate polynomial/map/block oracle;
this is not an independent sonic-quality certificate.

## Actual executable identity

Linux now opens the kernel's `/proc/self/exe` image directly when hashing the
executing program. Resolving that symlink to a pathname first could hash a newly
installed replacement while the original image was still executing. The shared
helper is used by the new publisher and the existing comp span worker. A bounded
actual-process probe renames the running file, replaces its old pathname with
different bytes, and checks that both image hashes still identify the original.
Windows uses the native executable path under the existing deployment contract.
Runtime library identity remains part of qualified packaging; the older nominal
stretch protocol's missing executable digest is not retroactively supplied.

## Acceptance and remaining work

The corpus first runs the real nominal/render/adopt/Undo/reopen and span workflow.
It then runs the new publisher in three actual processes over the stereo 44.1,
48 and 96 kHz source cases. Each process exercises a completed pair, late cancel,
active-writer refusal, no-overwrite retry, precancel, quota/identity refusal,
malformed requests, changed intent, and a forged within-blend sample with repaired
file/sample hashes. The separate scope review accepts that intentionally permitted
location, and complete recomputation correctly rejects the counterfeit audio.

Interruption injection covers a closed partial file, cancellation after the first
file's publication, and interruption immediately before paired completion. These
are observed exceptions/cancellation and preserved on-disk states, not simulated
proof of an actual killed writer, disk-full device, machine crash or power loss.
All failures leave canonical project state unchanged. The test probe's temporary
executable copy is retired and removed; its PID, before/after hashes and replacement
hash are retained in the process report.

[The source-bound receipt](../tests/results/M2/2026-10-14-comp-assembly-job/index.json)
binds implementation `c2ba7dbeb447eb4eadd189293d01396dda0442c6`, all relevant source,
libraries and six executables. CTest process 3906632 passes in 25.41 seconds with
all inputs unchanged before and after. Twelve nominal/span workers, three publisher
processes and one running-image probe retire. Three paired jobs complete, six
remain interrupted and three remain canceled. All 202,716 complete output samples
match recomputation. The original development corpus passed in 26.15 seconds.

Actual source/nominal/span/output/partial WAVs, project generations, original and
forged-then-restored job evidence, request owner references, process observations
and input receipts are retained in the archive: 7,864,322 bytes, 302 entries,
SHA-256 `37c27af8119d781142b6a3fdad087b84b5cc1acf8b7beaf0b55ec6d1665ee174`.
The deliberate corruption is described and observed in the test report; restored
original files are retained, not misrepresented as a corrupted final corpus.

`Id` does not define ordering, so the first compile failed at `std::set<Id>`.
The small uniqueness set now uses canonical UUID strings. The final build has no
new warnings; the inherited dependency CMake deprecation remains recorded.

Next: versioned trusted assembly-request references and undoable registration,
then desktop queued publication/recovery/close, shared live/offline post-SRC state,
and paired atomic adoption/Undo. Real raw/nominal/take anchors must remain intact.
Nonuniform/protected spans, broad layouts/channels/groups/containers, independent
alignment/sonic gates and the existing spectral/mixed-join artifacts remain open.
The full frozen professional DAW scope, Windows installation and European language
qualification remain incomplete. No installed preview, audio route, VM, equalizer
repository, production UI or dependency version changed in this checkpoint.
