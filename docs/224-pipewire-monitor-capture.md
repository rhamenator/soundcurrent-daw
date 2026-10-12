# PipeWire planar capture and explicit clock domains

2026-10-12. Full-suite completion remains unproven; the goal is active.

## Implemented dependency of the Linux native monitoring adapter

`PreparedPipeWireMonitorCapture` accepts actual graph-clock callbacks and planar
float buffers. It reserves its interleaved scratch before activation, copies float
bits in declared channel order and publishes raw audio before monitoring audio and
clock evidence. Both banks retain accepted data when a later clock publication
fails. It refuses invalid backing/quantum, unexpected rate, missing/reset timestamps,
clock changes, cycle gaps, position jumps/overflow, discontinuities and queue pressure.
Stop prevents new publications; EOF follows native callback join. The first fault
and its actual attempted clock remain immutable. Control-loop device loss without
a callback retains an absent clock rather than substituting a previous one.

`PipeWireMonitorCaptureStream` is a genuine input-only filter over the existing
PipeWire adapter. It requires explicit inactive input selection, creates no output
route, and joins the existing filter before finishing the capture banks. Its
`PipeWireCaptureEnd` reports joined callbacks and accepted extents; it does not
manufacture WASAPI Start/Stop/HRESULT/padding facts. Preparation declares an expected
graph rate; matching actual callbacks establish captured graph format. This is
graph audio, not proof of the underlying hardware rate or latency.

The clock handoff declares **Windows QPC** or **POSIX monotonic** 100 ns coordinates.
Separate publication methods refuse mixed domains. WASAPI capture/render contexts
also refuse POSIX handoffs before activation. The existing fitter field spelling
`Qpc100ns` is retained, but PipeWire explicitly uses `floor(nsec/100)` and retains
original nanoseconds in first/last/fault receipts. This conversion loses at most
99 ns of coordinate resolution; it supplies no project/audible anchor. Full per-packet
PipeWire metadata persistence and backend-neutral outcome/fitter declarations
remain required before these streams can be admitted as native project takes.

The [SPA clock contract](https://docs.pipewire.org/structspa__io__clock.html) specifies
monotonic nanoseconds and rate-relative positions. The installed 1.6.2 headers and
[tagged driver source](https://github.com/PipeWire/pipewire/blob/1.6.2/src/pipewire/impl-node.c)
confirm the cycle increments at each processing start. The callback accepts one
cycle increment with uint32 wrap, rather than treating normal progression as a
stream-generation change. Only rate numerator one is qualified here; other rational
clock representations remain explicitly unsupported at this boundary.

## Qualification

Implementation e978397ebdb90ab989585bace113d0b3e6d1e5d8 binds 794 source/checkout
inputs. Five Release suites pass in 19.66 seconds: the new capture boundary,
clock handoff, composed native startup and both WASAPI contexts. The capture suite
has 62,657 checks, including 1/2/3/16/256 channels, 8/44.1/48/96/192/384 kHz, short
and maximum quanta, exceptional float bits, cycle wrap, wrong domains, precise
first-fault evidence, scratch retirement and raw retention under pressure. Existing
native startup/context acceptance uses simulated SDKs; it does not prove physical
Windows routing. Known dependency compiler warnings remain in the original logs.

Two ASan/UBSan suites pass in 0.11 seconds. A genuine private PipeWire 1.6.2 daemon
also passes with Release and sanitizer binaries: each run captures 96,000 frames on
three channels in 375 packets and performs 576,000 sample-bit comparisons. Audited
source/capture callbacks show zero allocate/free/blocking-lock hits. The private
configuration instantiates only a dummy driver and explicitly selected test nodes;
it has no physical-device factories, session manager or speaker route. Its socket
is in an owner-only temporary directory; daemon and clients are joined afterward.
No desktop/default-device/volume setting or VM is changed. Leak checking is disabled
for sanitizer qualification; TSan/opaque framework ownership remain open.

Seven current Windows portable boundary/handoff/WASAPI-context/route/test/adapter
objects compile warning-free. No current Windows link/runtime claim follows from
that compilation. See the
[qualification packet](../tests/results/M2/2026-10-12-pipewire-monitor-capture/summary.json).
No new dependency or copied third-party algorithm is introduced.

## Retained failures and previous CI

The first private configuration omitted access assignment; source ports never
became ready. Adding access for its owner-only socket fixes this fixture setup.
The first native capture run then rejected ordinary cycle advancement after its
first 256-frame packet. Original source, receipts/configuration and logs are kept.
A diagnostic run identified cycle 2→3 with continuous position, matching rate and
no discontinuity flags; the corrected boundary checks progression and wrap.

PR158/run38295051968 predates this implementation. Linux passes all 144 tests in
367.91 seconds; Windows desktop passes 20 in 79.15 seconds, including the actual
93-check saved-take/timeline/Undo/Redo workflow; core cross-build passes. The native
job finishes its build and passes 70/71 tests, then is canceled approximately at
its 15-minute envelope while the final recovery test is running. This does not
establish recovery-test failure or an all-green native suite.

Its always-run concurrent-owner diagnostic succeeds twice, then fails on take16:
Stop revision32 is refused after CaptureFailed at engine frame152320. Original
facts show an 8,192-frame retained prefix, exhausted capture pool, an incomplete
writer with a 7,936-frame queued upper bound, and callback fault evidence. The
underlying scheduling/write cause is unresolved. All original job logs, diagnostic
logs and selected artifact JSON are retained; original ZIP/audio remains local.
The current native job envelope is 30 minutes to allow a complete suite and its
existing diagnostics. No per-test deadline, capture capacity, cadence, diagnostic
attempt count or failure threshold is relaxed; this does not resolve the owner
failure. No unchanged rerun replaces the original result.

## Next implementation task

Implement a PipeWire render boundary carrying real graph/lease facts, then connect
capture/render to the shared processing owner with explicit POSIX timing and a
versioned PipeWire outcome journal. Integrate that genuine route with the native
recording controller and Qt workflow; do not reinterpret PipeWire facts as WASAPI
receipts. Continue investigating the preserved Windows concurrent-owner failure.
The input-only stream is not the complete `NativeMonitorStreams` adapter and is
not yet offered as a new application recording route. Automatic recording alignment,
installed/physical workflows, later milestones and all-European localization remain
required.
