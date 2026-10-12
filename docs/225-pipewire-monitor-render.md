# PipeWire planar rendering and actual buffer-return receipts

2026-10-12. The full suite goal remains active and incomplete.

## Concrete implementation

A prepared render boundary now reads the existing interleaved monitor output bank
into explicitly selected planar PipeWire output buffers. It retains finite float
headroom and sample bits. Startup silence is bounded by frame and graph-time limits;
it does not consume monitor ordinals. A live underflow faults and queues quiet
output, recording consumed bank frames separately from monitor content actually
copied. Only a finished producer may use zero padding on its final short quantum.
Stop/cancel prevents new monitor consumption. Pending or duplicate/mismatched return
receipts cannot become successful completion.

The existing PipeWire filter exposes an optional **completed-cycle** callback after
all attempted input/output returns. Fixed, preallocated per-port facts carry native
acquisition status, whether a buffer was acquired, and the exact `queue_buffer`
result; absence means the native operation was not called. The old boolean release
API remains intact, including no duplicate native return. Compile-time checks bind
the internal/public status mappings. The completion callback receives scalar facts,
not borrowed native sample pointers. Normal callers need not enable the hook.

`PipeWireMonitorRenderStream` is a genuine output-only stream with explicit inactive
routing. Its core binds separate process and return clocks, preserves per-channel
results and increments queued monitor frames only when every expected output buffer
was successfully returned. Content completion is published after the actual final
return. It remains distinct from callback join, speaker playback, queue drain,
startup/warmup silence and final padding. No WASAPI padding, Stop or HRESULT result
is fabricated. The stream joins the existing native filter before publishing EOF.

Clock admission uses the explicit POSIX domain and actual graph positions/rate.
Expected rate and bounds are declared before activation; matching callbacks establish
the observed format. Other rational clock representations, hardware latency and
an automatic project-time anchor remain unqualified. The engine still uses the
existing daemon and framework-independent bounded banks.

## Current qualification

Implementation b8ad15e binds 801 exact source/checkout inputs. Six focused Release
suites pass in 19.62 seconds, including the output bank, capture regression, native
buffer protocol, clock handoff and composed native startup. The final return-shape
update reruns the render suite successfully (30,443 checks). Cases cover finite
partial tails, signed-zero/headroom/channel order, declared rates/layouts up to256
channels, warmup limits, underflow, Stop/cancel, queue pressure, missing/foreign
receipts, exact positive/negative/absent per-channel results and resource retirement.
The new fixture indentation warning is retained and corrected; dependency warnings
remain in original logs rather than suppressed.

Three ASan/UBSan suites pass in0.08 seconds, with leak checking disabled. Release
and sanitizer binaries each render96,123 monitor frames into a genuine private
PipeWire1.6.2 sink. Each run verifies288,369 float-bit values, observes133 zero frames
in the final256-frame quantum and exact native queue result0. Audited render
callbacks have zero allocate/free/blocking-lock hits. The private owner-only socket
and dummy-driver/test-node configuration have no physical devices, session manager,
speaker/default route or volume change. Each daemon/client is joined afterward;
no VM is used. Queued and received test-sink frames are not proof of physical output.

Five current Windows portable render/fixture/owner/adapter objects compile without
warnings; no current Windows link/runtime claim follows. See the
[qualification packet](../tests/results/M2/2026-10-12-pipewire-monitor-render/summary.json).
No new dependency or third-party algorithm is introduced.

## Capture regression failure retained

A separate private capture regression run under the same low-priority policy
captures49,408 matching raw/monitor frames, then refuses an actual graph gap:
cycle194→196, position16917049981→16917050493, duration256, unchanged clock10/rate48000,
and no explicit xrun/discontinuity flags. The first fault is retained before any
missing interval can enter the raw bank. The wrapper joins both clients/daemon.
Its full configuration, fixture source and original logs are retained. No unchanged
retry replaces it with a passing run, no continuity check or quantum is weakened,
and no precise scheduling/overhead cause is inferred. Capture throughput under
contention remains unqualified; this is correct refusal evidence, not a96,000-frame
capture success claim for this revision.

## Previous Windows CI failure retained

PR159/run38298826349 predates rendering. Linux passes145 tests/433.28 seconds;
Windows desktop passes20/83.21 seconds and the core cross-build passes. The native
suite executes all72 tests with one failure: finite native recording startup reaches
its original five-second wait on the44.1kHz mono-to-stereo case. Before cancellation,
raw/monitor acceptance and source copy are88,641 frames, published/delivered monitor
frames are0 and a Stop is planned. The retained native receipt has200 warmup attempts,
no acquired startup-ready snapshot and no content callbacks. Capture and render
finish only during explicit cancellation. These facts do not establish a particular
clock, scheduling or I/O root cause. All three subsequent original concurrent-owner
diagnostics complete20 takes; that does not erase PR158's separate take16 failure.

The fake SDK now records independent atomic wait-stage, raw/written packet and
current coordinate observations. Timeout captures them **before** cancellation and
retains them with the original metadata afterward. They are explicitly not a
coherent device-time snapshot. No production behavior, deadline, fixture pacing,
queue capacity or failure threshold changes to make the failure disappear. The
current failure remains unresolved pending stronger evidence/reproduction.

## Next implementation

Combine genuine PipeWire capture/render with the shared processing owner, add
versioned PipeWire per-packet/return provenance and backend-neutral lifecycle
receipts, then wire the route into the native controller and Qt workflow. Extend
capture-side post-return evidence before calling the complete adapter qualified.
Investigate the retained Windows startup and contention failures without substituting
successful retries for causal evidence. Automatic recording alignment, installed
and physical routes, GUI TSan/permanent ownership, later milestones and reviewed
European localization remain required. This is a render dependency, not completed
`NativeMonitorStreams` integration or frozen-reference parity.
