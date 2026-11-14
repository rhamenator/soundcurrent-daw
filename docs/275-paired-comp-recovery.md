# Durable paired comp selection and recovery

Checkpoint 275 adds a control/disk API for preserving and inspecting the two
span requests belonging to one comp edit. It is framework independent and runs
outside audio callbacks. It does not yet launch, reserve memory for, or reap
children through the desktop controller.

## Trusted selection

`CompPairSelection` owns the recipe, executor identities, worker policy, derived
requests and canonical manifest under resource credits. A child is omitted only
when the recipe derives `ExistingMedia` for that fragment. Even pairs with no
children must pass worker-policy and identity validation.

Publication preserves immutable recipe and request bytes before publishing the
selection manifest. A cooperative publication lease serializes writers; owned
temporary files are flushed and published without replacing existing data.
Identical publication is idempotent. A conflict is refused. Interrupted publication
can be retried with the original trusted selection.

The manifest digest must be stored by the trusted owner independently of the
sidecar. Reopening checks that digest before interpreting fields, loads the recipe
with a fresh project context, rederives both requests, and requires exact canonical
manifest and child-request bytes. Saved epochs are not restored as authority.
This checkpoint's test uses an external owner-reference fixture; durable trusted
references in the actual project schema remain to be implemented.

## Child states and controls

Inspection distinguishes unneeded, absent, active, waiting for start, interrupted,
canceled, complete, stale and invalid children. Active writer ownership prevents
candidate verification. Cancellation is recorded while a child remains active;
it becomes a retired canceled state only after ownership is released. A valid
committed completion takes precedence over a late cancellation marker.

Retired completions pass the existing independent source, intent, grants, full
RF64/PCM and completion verification. Both needed children must complete before
the pair is ready. A stale project or context rejects readiness, including when
both children are unneeded. Inspection leaves the session unchanged.

Start verifies current session/context, active writer ownership, original readiness
and saved intent before writing an immutable acknowledgement. Retired, canceled
or completed children are refused. Cancel remains callable after model changes.
These APIs assume a serialized owner; they are not a process supervisor. Process
liveness races, child grant reservations, spawning/reaping, close barriers and
project-owner persistence need their controller integration tests.

## Actual acceptance evidence

[The frozen receipt](../tests/results/M2/2026-10-14-comp-pair-recovery/index.json)
binds implementation `f33d45b358b9aa6b575ce7a4551d1814aabb5d8c`, input/library/
executable hashes and CTest process 3843352. The source-bound Linux gate passes
in 2.20 seconds with four actual workers: two complete a pair, one is canceled,
and one retires through its unchanged 500 ms watchdog while awaiting start.

The corpus checks repeated publication/reopen, active state, stale-start refusal,
retired-start refusal, cancellation after retirement, changed manifest/request/
PCM/source refusal, late cancellation, and no-render pair recovery with stale
context refusal. Interrupted-after-start inspection uses a synthetic retained
start marker on the already retired watchdog case; it does not claim an actual
mid-render crash or power-loss test. Paths include accented and Japanese names.
The fixture's baseline remains controlled analytic media with declared nominal
geometry, rather than an independently qualified nominal producer.

All process observations, candidate/source WAVs, requests, saved selections,
control/completion markers and logs are frozen in a 2,924,910-byte, 106-entry archive:
SHA-256 `dbc0ae3267c31de7604d916b02e4bba6ac9f84399c3614787f64411daefbe349`.
This establishes recovery and integrity checks, not sound-quality admission,
final join assembly, curve/alignment, post-SRC representation or model adoption.

## Platform and remaining work

Original [CI 38607309278](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38607309278)
at `27b84de` passes all four jobs, including the preceding actual owned-span worker
on native Windows. Its original terminal APIs, full logs and all five artifacts
are retained before this head update; server digests, sizes and ZIP CRCs match.
[Retained review](../tests/results/M2/2026-10-14-comp-pair-recovery/pr194-worker-ci/index.json)
binds Linux 177 suites, native Windows 93 plus nine explicit worker/core gates,
and desktop 22. The original native corpus has 33 workers and six completed spans.
The independent payload audit verifies 14 deployment files, 1,590 corresponding
source files, all collection hashes and both pinned media DLLs. This does not qualify the new pair API on
Windows; its new hosted gate retains its own process evidence.

No production GUI source changed. Localization extraction still has 1,038 keys,
34 partial catalogs, 3,168 drafts, zero reviewed and zero fully UI-qualified
languages. Installed previews, audio routes, equalizer worktrees and VMs are
unchanged. There are no new third-party dependencies.

Next, add trusted paired references to project state and a supervised desktop
owner with memory reservations, start/cancel/close and reaping. Then assemble
verified spans within physical/post-SRC permissions and independently qualify
curve/alignment/scope before adoption. Nonuniform/protected production, existing
spectral artifacts, full quality, native installation and all frozen-reference/
European localization workflows remain required.
