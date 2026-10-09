# Resource-admitted large projects: first implementation

X006 retains no fixed product/license ceiling on total project tracks. This
checkpoint replaces the foundation's 256-track validation/parser/mix/replacement
mask assumptions. It is an audio-track state and synthetic processing checkpoint,
not completion of X006 or a real-time throughput promise.

## Trusted admission

`StateBudget`, `ProjectBudget` and `MixConfig` belong to the caller/application.
Project files contain no resource policy and cannot raise it. Defaults charge
64 MiB for state/validation, 32 MiB for encoded project bytes, 256 MiB for JSON staging
and 128 MiB for prepared DSP/playback payload. These are configurable byte budgets,
not a substitute track-count constant. `ResourceLimitError` carries the resource,
required/available bytes and an explicit arithmetic-overflow flag. Refused edits
and publication keep the accepted canonical state and snapshots intact.

Charges cover declared owned payload and bounded validation/parser work. They
are not an allocator-overhead or process-RSS guarantee. Simultaneous old/new
models, retained history, graph replacements/tails, GUI objects, shared media and
other processes need their own measured envelopes; this checkpoint does not
claim that those combined envelopes are fully admitted.

Session admission accounts for vector capacities, strings, identifiers,
route/clip/band/asset data and validation indices using checked size arithmetic.
There is no fixed track, asset or aggregate clip/route count in the canonical
validator. Per-bus 256-channel, per-EQ 64-band and individual text/shape contracts
remain explicit and separate. `ValidatedSession` is a control/preparation-only
immutable borrow with stable-ID lookup. It must not outlive or overlap mutation
of its source. Public construction always validates; the shared mixer prepares
its EQ lanes through this token instead of revalidating/searching the entire
session for every lane. Group-history differences/order restoration now use
indices rather than repeated whole-track scans.

Playback allocates its track replacement mask before activation and charges it
to the prepared payload. Each callback clears/reuses that prepared capacity;
it never resizes, allocates, frees or locks to grow a track mask. Duplicate and
out-of-range live replacements are rejected before consuming a pipe or moving
the shared cursor. Track ordinals remain generation-bound prepared data; stable
IDs, rather than ordinals, persist in projects.

## Project format 1.7

The writer emits schema 1.7; readers retain migration of 1.0–1.6 with the same
musical state. Older readers refuse the new minor version before interpreting it.
The public budget is supplied separately to encoding, decoding and ProjectStore.
The old 4 MiB hard byte bound is replaced by the caller's encoded-byte policy.

Decoding checks bytes before parsing, performs a non-building SAX pass with
charged nodes/strings/key work and the existing bounded nesting/duplicate-key
refusal, then builds a DOM. A conservative canonical staging pass runs before
constructing canonical vectors/strings; final state admission also checks their
actual capacities. Encoding admits state and staging before building its DOM and
checks encoded bytes before publishing. Unknown fields, malformed types, unsafe
paths, invalid IDs/graphs and unsupported versions retain refusal behavior. No
partial load or truncated Save is presented as success. Full streaming parsing
and larger portable asset/cache workloads remain future work.

The retained predecessor executable actually refuses a 257-track 1.7 project with
`Unsupported project schema`, leaving both snapshots unchanged. Its 4096-track
attempt stopped earlier at the predecessor's 4 MiB byte bound. An initial assertion
that expected the schema diagnostic on that larger file was wrong; original logs
are retained and it is not relabeled as a schema-gate experiment.

## Desktop and recording adoption

The timeline Add Track control no longer disables itself at 256. The control
worker performs transactional state admission. ControllerOptions carries trusted
project admission to its I/O owner and history. Grouped recording receipt shape
checks use a charged receipt budget rather than a fixed 256 receipt ceiling;
actual per-take/backend recording-arm/native channel capacity is still separate.

The actual Qt desktop test opens 4096 tracks, selects/renames the last stable ID,
adds 4097, performs Undo/Redo and Save/reopen. It uses the real StudioWindow,
canonical controller and disk path; no native audio activation occurs. The first
new UI test checked the row count and selection immediately after a worker-side
canonical update. It failed without logging which subterm missed. The subsequent
instrumented observation sees 4097 canonical tracks, 4096 rows and the previous
selection before GUI polling. Waiting for the GUI projection then completes the
workflow. The original failure and missing unsaved state/subterms stay explicit.
The first sanitizer run passes seven affected groups, including the 4,096-track
core fixture, but hits the ordinary ten-second Close helper deadline on the new
large desktop case. Its saved 4,096-track project, accepted 4,097-track snapshot,
source, executable and log are retained. The exact Save-worker timing was not
logged in that original. The large desktop workflow now has a separate bounded
CTest case, a scoped 60-second Close wait and explicit error/state/duration
diagnostics; ordinary desktop waits/deadlines stay intact.
The separate sanitizer desktop case passes in 54.15 seconds, with Close/Save
measured at 16,536 ms; ordinary timeline tests pass in 4.10 seconds. Seven other
affected groups already passed on unchanged production sources, including the
132.59-second resource-admitted core fixture. This is scoped sanitizer evidence
across those runs, not a single nine-group rerun or a responsiveness promise.
This does not qualify virtualized views, physical display operation or Windows UI.

## Acceptance scope and remaining work

Core workloads 257/512/1024/4096 preserve all IDs/routes through grouped edits,
Undo/Redo, Save/reopen/previous snapshot and explicit low-budget refusal. Two
synthetic 16-frame blocks per project exercise exact float64-matrix/float32-output
summing, a high-ordinal live replacement and parameter acknowledgement, invalid
replacement refusal and zero callback allocations/frees/blocking locks. The
largest prepared playback payload is about 1.64 GB: per-track bounded event queues
are significant. These tests are not sustained playback, independent new EQ
filter-design quality, plugin processing or physical interface qualification.

Required next work includes shared bounded media handles/cache/read-ahead,
virtualized views and metering, configurable desktop resource controls, measured
snapshot/history and combined graph memory, large recording/adoption workflows,
freeze/bounce, worker scheduling, and sustained Linux/Windows/physical profiles.
The current recording owners/backends still have 256-arm/channel assumptions;
those must be separated from project track inventory, not silently ignored.
The 64-operation structural batch and 256-command/32 MiB history policies also need
appropriate large-group workflows without losing atomicity or safe admission.
All 92 frozen contracts, X004 imports, X005 profiling, European language delivery
and independent Windows native/UI/install qualification remain open. Original
native sanitizer 71's active gap/source CPU cause and historical failures remain
unresolved; no native observation is added by these synthetic tests.

See the [dated receipt](../tests/results/M2/2026-10-07-resource-admitted-projects.json)
for exact source/executable/archive hashes, original failures and qualification.


## Configured controller preflight review correction

Automated review of the first published head found that controller routing and
structural preflight calls still used the default state policy, even though the
store and history had the configured policy. They now pass `admission.state`
explicitly before committing an active gesture. This covers both directions of
policy change, including raising the budget above the default 64 MiB.

A separate regression uses a small trusted 32 KiB policy and an oversized route/
track edit. The original executable refuses both changes later in history but
incorrectly commits an unrelated active parameter gesture; Cancel then leaves
the changed gain. Both original observations and saved projects are retained.
The corrected preflight must refuse before gesture/history mutation, with Cancel
restoring the original model and saved project unchanged. This bounded test
qualifies policy propagation and transactional refusal; it is not an above-64 MiB
project workload or Windows runtime measurement. Branch protection refused the
initial merge while the review conversation was unresolved; no bypass was used.

See the [supplementary review receipt](../tests/results/M2/2026-10-07-resource-admission-review.json)
for the corrected source identity and tests. The initial checkpoint receipt and
archive retain their original source identity and results.


## Subsequent shared media checkpoint

[Shared media/cache implementation](79-shared-media-cache.md)/ADR064 adds one
resource-admitted asset registry, handle pool and decoded-page cache per serialized
read owner, with bounded file-backed257/1,024-track and96-asset exact-output tests.
The evidence above retains its original source/scope; this subsequent checkpoint
does not qualify sustained native, full UI/recording or Windows runtime capacity.

## Configured 8192-track viewport gate (2026-10-09)

Schema1.9 increases the conservative JSON-to-canonical preflight charge. The first
hosted run retained a64MiB refusal for the8192-track viewport fixture; native
Windows core28/Qt8 and the new processing workflow passed separately. The viewport
fixture now declares96MiB through trusted ControllerOptions/ProjectStore, asserts
the unchanged64MiB default refusal, and retains all8192 tracks and existing paint/
hit/identity/history/raw-media/Save-reopen checks. Local affected Release3 tests
pass. This does not expand production defaults or infer arbitrary capacity from
a track count. User configuration of additional state/parser admission grants
remains a separate X006 UI/product gate. New source/full/native gates are distinct
from the preceding failing hosted source.
