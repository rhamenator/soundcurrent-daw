# PipeWire processing startup and stop handoffs

Checkpoint228 connects the PipeWire planar boundaries to the shared processing
owner's startup and finite-end contracts. A complete live native thread owner and
controller/Qt route are still required; this is not workflow parity qualification.

## Backend-neutral processing end status

Processing now accepts a fixed-cell `MonitorCaptureStopHandoff` interface for its
request and acquired completion status. The existing WASAPI implementation keeps
its original Start/Stop/HRESULT receipts unchanged. A separate PipeWire implementation
retains actual `PipeWireCaptureEnd` facts, validates joined/accepted/native extents,
and reports Healthy, NativeFailure, InvalidReceipt or OwnerLost. A missing owner
has no original end. No Windows native result is manufactured from POSIX facts.

The PipeWire capture boundary checks the immutable processing request before
copying another packet. After callbacks join, it publishes its original end before
clock/raw EOF. Payload/postroll admission, generation and capture geometry are
checked before binding; native return tracking is required with this handoff.
Two owners publish one request and one end using release/acquire fixed cells.
Virtual interface calls do no SDK work, disk I/O, allocation, wait or lock.

## Native startup permission

PipeWire render can now bind the shared immutable startup handoff. A filled PCM
bank alone cannot authorize a pull. The processing owner must publish a matching
admission with enough queued output and original startup/EQ/channel-map provenance.
Render checks identity, rate, quantum, generation, retained capacity, current time
and untouched output extent before acquiring Ready. It publishes moving original
render clocks while returning bounded warmup silence. Future, foreign, faulted,
lost-owner or canceled readiness refuses content; cancellation remains effective
after Ready. Ready evidence is only readable from the render context after join.

Review also separated independent current monotonic time from the original graph
clock: readiness can be published between graph observation and the render read.
The genuine render adapter samples CLOCK_MONOTONIC when startup is bound; a
missing/failed sample refuses permission. Tests cover current readiness newer
than the preserved graph timestamp, future readiness relative to current time,
and missing current time. No synthetic current-time value is used by the SDK.

The common 100 ns type retains its legacy spelling. The declared source domain
still distinguishes Windows QPC from floor(POSIX monotonic nanoseconds/100).
No cross-domain fit, project placement, audible completion or hardware alignment
is implied.

## Qualification

Implementation **2f2c3e4** binds813 source/checkout inputs. Seven current Release
suites pass20.18s; isolated ThreadSanitizer passes0.06s without suppressions
(16 actual processing/native thread pairs, media/resampling/SDK disabled).
Six current ASan/UBSan suites pass3.76s with leak detection disabled. The
genuine PipeWire1.6.2 render adapter compiles with strict warnings and actual
current monotonic sampling; its combined startup route is not yet executed.
Seven current Windows processing/handoff/boundary/fixture objects compile
warning-free; no current native Windows SDK/link/runtime claim. See
[the exact qualification packet](../tests/results/M2/2026-10-12-pipewire-processing-handoffs/summary.json).

A serial composition fixture sends a headroom-preserving 1 kHz source through
real prepared PipeWire planar contexts, shared clock fitting/resampling, a -6 dB
peaking EQ and mono-to-stereo weighted mapping. It verifies 2,400 finite monitor
frames, startup permission, a processing horizon stop, native-result facts, exact
raw sample bits and the durable paired journal on inspection. Native buffer-return
inputs are synthetic; no SDK, physical device or complete concurrent route owner
is qualified by this fixture.

Separate fixed-cell tests cover lost/invalid/native-failure ends, duplicate/foreign
requests, admission/quota refusal, filled-bank-before-Ready, future/terminal/cancel
readiness, and actual two-thread stop/end publication. Guarded callback/processing
and both thread methods have zero allocation/free/blocking-lock audit hits.
The isolated stop target works without media or resampling and enables narrow
ThreadSanitizer qualification independent of opaque frameworks.

The initial negative fixture changed output rate while retaining a rate-specific
EQ fingerprint; the handoff correctly refused it before render binding. Corrected
fixture changes render identity so a structurally valid foreign declaration reaches
the intended binding gate. Original source/logs remain retained; validation is not
relaxed. Existing unrelated build warnings remain recorded.

Original PR162/run38310727076 passes all four jobs: Linux149/287.14s,
Windows native76/206.49s, desktop20/82.26s and core cross-build. The new durable
journal's Windows process-interruption tests therefore ran on MSVC. Original logs,
selected artifact members and exact source bindings remain retained. This does not
causally resolve earlier startup/pool/capture-clock-gap failures.

## Next implementation task

Create the owned PipeWire route/thread factory with one capture callback owner,
one render callback owner, one processing worker and one serialized raw/receipt
disk consumer. Join before settlement and persist route/EQ/map intent and native
terminal/fault facts using versioned states. Qualify genuine private combined
capture/EQ/render/journal/recovery, then native controller and Qt integration.
Keep raw recordings separate from monitoring correction and use explicit timeline
placement until a verified native timing anchor exists. Installed/physical quality,
Windows workflow parity, unresolved contention, later milestones and reviewed
European localization remain required.
