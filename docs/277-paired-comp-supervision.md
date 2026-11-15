# Paired comp process supervision

Checkpoint 277 adds a desktop control service using the real project controller's
immutable snapshots and the framework-independent paired recipe/recovery APIs.
It is a candidate-producing service. Final comp assembly/adoption and the main
window controls are not connected yet.

## Ownership and admission

`CompPairController` admits one queued operation and runs at most one child at a
time. Its thread owns filesystem/protocol/QProcess work. GUI methods enqueue,
request cancellation/shutdown, or poll an immutable snapshot. A caller supplies a
thread-safe project snapshot provider that must outlive asynchronous close.
The application helper path is trusted configuration; it is never loaded from a
project file. Its actual hash must match the selected binary.

The saved project reference must exist on disk before rendering. An approved,
bounded, cancellable read decodes that reference independently of the live model.
It does not reread unrelated audio to establish this reference. Recipe/source
admission and final candidate verification remain the existing separate checks.

The application can join the project owner's memory ledger. The full child
ceiling is reserved before launch and retained through reaping. Input/channel
work and retained process logs have separate credits. Logs are bounded and remain
available with terminal PID/exit/readiness/reaping observations. These are payload
and child-process resource controls, not a privilege sandbox or an exact Qt/RSS
measurement. The existing dependency/owned-filesystem trust contract remains.

## Start, cancellation and project changes

The service checks live root, epoch, closing/opening state, retained reference and
recipe-relevant model state before launch, during observation, before the start
acknowledgement and after candidate verification. Changed immutable model prefixes
are revalidated; unchanged snapshots avoid repeated whole-model validation.
A later model edit can still occur after a sampled prefix. Final adoption must
therefore perform its own canonical-controller guard; this service does not
supply an adoption token.

Readiness must match original request and policy. The owner checks the process
and active job lease again before start. Cancellation/stale ownership/deadline or
channel overflow stops further children. A cancellation marker is attempted;
cooperative retirement is followed by termination after 100 ms and kill after
300 ms when needed. Reaping always precedes child-credit release. Requesting
shutdown is asynchronous; closed is published only after owned child retirement.
The destructor joins as a fallback outside audio callbacks.

Read-only review never launches a child. A render request can reuse independently
verified committed children and render only absent siblings. Active, interrupted,
canceled, stale or invalid existing jobs require explicit recovery instead of
silent resume/overwrite. Cancellation after the first child's commit preserves
that candidate while the second child's canceled state remains visible. Failed
or canceled processes are inspected after reaping; committed markers retain their
independent completion authority, without automatic model adoption.

## Actual acceptance

[Source-bound receipt](../tests/results/M2/2026-10-14-comp-pair-supervisor/index.json)
binds implementation `f5d0f44650594026dd2528fcf182f69e8bc46102`, source/library/executable hashes and
CTest process 3871228. The Linux Release gate passes in 4.06 seconds
with ten cases using an actual `ProjectController`, shared parent/child ledger,
QProcess and the real span worker. Nine child processes retire; three spans
complete. Cases cover a completed pair, read-only review/completion reuse with no
new process, cancellation, live clip edit, asynchronous shutdown, missing helper,
insufficient memory before spawn, actual project replacement, cancellation of the
second child preserving the first completion, refusal of an unsaved reference,
and an actual unchanged 500 ms worker watchdog exit.

The frozen packet retains actual sources/projects/previous generations, recipe/
selection/request/control/completion files, stdout/stderr, terminal observations
and all input/process receipts: 9,651,934 bytes, 200 entries,
SHA-256 `f44dfec6fa55b94c7a3568afac4bf0581296fb8fc5db6460076c1f3392ea3a36`. Source and binaries remain unchanged across
the run. Fixtures use controlled stereo analytic nominal media, not an
independently qualified original nominal producer. Sound quality is not admitted.

## Original Windows predecessor and remaining work

[Original CI 38611054401](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38611054401)
at `1b2c651` passes all four jobs: Linux 178, core cross-build, native Windows 93
plus ten explicit worker/pair/core gates, and desktop 22. Original terminal APIs,
full logs and all five ZIPs are retained before the head update; sizes/server
hashes/CRCs match. [Retained review](../tests/results/M2/2026-10-14-comp-pair-supervisor/pr194-pair-ci/index.json)
includes the original native pair corpus: four actual workers/two completions.
Independent audit verifies 14 payloads/1,597 Windows-filtered source files, all
collection hashes and both pinned media DLLs. This qualifies the preceding pair
API, not schema1.21 or this new supervisor. Native/main-window/audio/installer
qualification for those new paths remains pending.

Actual localization extraction update/check remains 1,038 keys, 34 partial
catalogs, 3,168 drafts and zero reviewed or fully UI-qualified languages. No main
window, audio route, installed preview, VM, equalizer or dependency changes.

Next, assemble independently verified physical/post-SRC candidates within their
permissions and verify curve/alignment/full scoped PCM before adopting an atomic
paired edit. Integrate supervised preparation/recovery with main-window comp and
close barriers when that result has sufficient authority. Post-SRC needs shared
live/offline prepared state; nonuniform/protected maps require their producer.
Broader channel/rate/container/group/native tests, spectral/join artifacts and
full processing-quality, installation and European localization remain open.
The full frozen professional DAW scope remains active.
