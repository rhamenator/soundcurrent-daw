# Owned comp span worker and independent completion verification

Checkpoint 274 runs the exact uniform span renderer in a disposable process with
owned request/job/completion identity. It produces an integrity-checked physical
candidate, not an adoption token. Parent GUI/controller integration, paired result
assembly, curve/alignment review, recovery inventory, nonuniform/protected maps,
post-SRC representation, and sound-quality gates remain open.

## Request and executable identity

`sc-comp-span-v1` carries child operation, parent operation/recipe digest/canonical
recipe bytes, parent recipe-executor identity, selected fragment, and span
processor/executable identity. The child receives the trusted request SHA-256
through control arguments and compares it with the exact input bytes. It obtains
its actual executable path from the OS, hashes it, and requires the request's
span executable identity to match. The parent recipe executor is a separate
identity; the span executable is not presented as the final quintic-join producer.

The bounded codec checks duplicate fields, depth, scalar/container counts and
parser staging before allocating the DOM. The recipe's separate codec recomputes
permissions/source closure and the selected span. Integer/boolean types, child
identity, exact fractional grid, settings and uniform slope are checked. The
existing project/anchor schema is unchanged. Nonuniform maps still require their
producer and are refused without flattening.

The control owner admits the containing process grant before spawning and holds
it through reaping. CLI grants cover memory, source/output bytes and deadline.
The worker applies Linux address-space or Windows process-commit limits before
input parsing, disables Linux core dumps, and starts a process-lifetime watchdog.
These are process/resource controls, not a privilege sandbox. Runtime dependency
qualification and the immutable owned-filesystem contract remain required.

## Source admission and job lifecycle

Before creating an output operation, the worker hashes/decodes the actual raw,
current, nominal and retained sources and checks declared companion extents/hashes.
Companion capture semantics still need their existing native gate. The jobs root
must be the selected project's `media/comp-spans`; roots/ancestors are plain,
canonical absolute directories. A fresh child directory and exclusive writer lease
prevent reuse of an existing operation.

The flushed intent contains request/recipe/executable/source identities, exact
origin/span/step/frame counts, settings and grants. Readiness repeats that identity;
the parent must verify it before writing `start.request`. Readiness here means
metadata/source admission, not full sound-quality or backend-window admission.
Cancellation and the hard process watchdog remain active while awaiting start and
through the renderer. Short backend windows produce an incomplete owned job.

The child writes finite float32 RF64 candidate PCM, then closes/flushes it, checks
its file grant and hash, and rechecks sources and executable bytes. Audio is
published without overwriting. The completion marker is separately flushed and
published without overwriting; no cancellation check occurs after that commit.
Ambiguous publication failures report that a marker may have committed, so the
parent must inspect the owned result after reaping. Incomplete job directories and
partial/published audio without a valid completion remain available for later
recovery policy. This checkpoint does not implement the recovery inventory/UI.

## Independent parent verification

`verifyOwnedCompSpan` refuses active or missing writer ownership, compares intent
and completion against the original request/grants, and admits all source files
again. It independently verifies the RF64 format/frame/rate/layout, full file hash,
streamed sample hash, finite PCM and peak. Sources, intent and completion are
rechecked before returning a move-only candidate result bound to request digest.
The caller must separately check current session/context before further use.

This verifies completion/integrity. It does not independently reperform the R3
algorithm or qualify alignment, the inner blend curve, permitted final edits,
post-SRC execution or audio quality. Combine it with the existing actual-file
scope review and the remaining producer/curve/quality checks before adoption.
Original raw/nominal assets, old unity verifier/model gates and installed previews
remain unchanged.

## Actual source-bound process acceptance

[Frozen receipt](../tests/results/M2/2026-10-14-comp-span-worker/index.json) binds
implementation `d928d72f2c7df19a7f19a5a5826eba3583dff563`, source/library/worker/
fixture hashes and actual CTest PID 3830389. The owned-worker workflow passes in
5.02 seconds on a fresh dedicated Release build with one low-priority worker.

The corpus launches 33 actual workers across 44.1/48/96 kHz source rates. Six
pitched/fractional spans complete and are independently verified; 27 workers
exercise refusal/interruption paths. Tests cover parent readiness, refusal while
a child is active, cancellation before start, watchdog expiry without start,
existing-operation/no-overwrite preservation, invalid fragment type/value,
wrong processor/binary hash, changed trusted request bytes and changed source
media before directory creation. Parent verification rejects changed result PCM,
completion fragment, source bytes, canceled verification and stale clip state.
Every process is retired; worker binary and original source bytes remain unchanged.

The current baseline fixture is a controlled analytic WAV with declared nominal
geometry; it does not claim an independently qualified original nominal producer.
The span candidate is rendered by the actual child R3 implementation. Six candidates,
all requests/intents/ready/completions, source WAVs, logs, partial/interrupted job
state and process/input/binary receipts are retained: 3,675,554 bytes, 169 entries,
SHA-256 `3f4538ae507de0fce5bd4cfded3997a47f4a99774309bf48aa4b0b29ded68e03`.
No audible/VM test, model edit, live/offline overlay or quality admission is claimed.

## Original native span predecessor

[Run 38602907153](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38602907153)
at `496b313` passes all four jobs: Linux 176, Windows core cross-build, native
Windows 93 plus eight explicit span/media/recipe/core gates, and desktop 22.
Original terminal APIs/full job logs/all five ZIPs are retained before updating
the head. [Retained review](../tests/results/M2/2026-10-14-comp-span-worker/pr194-span-ci/index.json)
includes the actual native span report extracted from its original ZIP: 36 R3
renders, 2,948 checks, fundamental maximum error 0.506%, and three dominant peaks
outside the fundamental guard. Quality remains false on Windows as on Linux.

Independent build-input review verifies 14 payload files, 1,582 corresponding
source files with actual Windows checkout filters, all collection hashes and both
pinned media DLL builds. Main-window/native-audio/installer flags stay false. New
owned-worker Windows execution needs its own CI run; prior native rendering alone
cannot qualify this process protocol. New CI retains owned-worker evidence folders.
No production GUI changes; localization extraction check remains 1,038 keys,
34 partial catalogs, 3,168 drafts and zero reviewed/fully UI-qualified languages.

## Next implementation

Connect supervised paired jobs to controller state and durable inventory/recovery,
then assemble candidates with the declared physical/post-SRC permissions and
independently check curve/alignment. Add post-SRC state/prepared overlay shared by
live playback and offline export; keep paired Undo, stale root/epoch, cancellation,
close/reopen and original source/anchor preservation. Implement nonuniform/protected
span production without rounding its interval geometry. Broader channel/container/
rate/group/native testing and full pitch/formant/phase/protection/transient/listening
quality remain required. All frozen M2–M11/F/Q/C/N/X004/X005, Windows installation
and all-Europe workflows remain active and incomplete.
