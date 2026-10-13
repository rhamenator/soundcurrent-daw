# Native input returns and versioned PipeWire receipt handoffs

2026-10-12. The complete suite goal remains active and incomplete.

## Implemented provenance dependency

Genuine PipeWire capture now requires a post-return callback for each admitted
process cycle. The core retains separate process and return clocks, per-channel
acquisition/neutral-media status and exact optional queue results. Raw/monitor
acceptance and successful native buffer return are distinct extents. Missing,
foreign, invalid or failed native returns cannot erase an already copied raw
packet, and cannot become successful return counts. Stopping can return native
input buffers without accepting more raw samples; those counts remain separate.
The portable raw-only seam may explicitly omit returns. A receipt callback requires
return tracking, and the genuine stream always enables it.

Capture and render can publish full scalar receipt copies through independent,
fixed64-cell SPSC lanes to one serialized journal consumer. Configured capacity
(1–64), logical sequence bounds, consumer loss and malformed geometry are admitted
explicitly. Faulted lanes retain the accepted prefix and the first rejected original
record. EOF follows producer finish; a terminal acquire and second queue read
prevent the final publication from being overtaken by EOF. The lane payload is
charged before internal allocation. Publication/read perform no allocation, wait,
lock, file I/O or logging. Receipt pressure faults the emitting stream while retaining
raw samples and native return facts. No producer silently overwrites old receipts.

A disk/control codec now encodes capture/render records in bounded little-endian
**SCPWREC1, schema1.0**, with an explicit POSIX monotonic nanosecond domain. It retains
signed delays/SDK results, unsigned clocks/sequences, absent versus present results,
roles, flags and active per-port rows. Unknown versions/domain/role, malformed
booleans, truncation, oversized/trailing data and inconsistent claimed success
refuse. Unused fields cannot silently disappear. Failed/absent native facts remain
failed/absent. Structural decoding alone proves neither PCM, continuity, route
ownership nor project alignment.

This is an in-memory handoff and codec. The project recording journal writer,
checkpoint/recovery binding and complete shared processing/controller integration
are still required. No new GUI recording route, installed release, automatic
alignment or WASAPI Stop/HRESULT inference is introduced.

## Qualification and retained corrections

Implementation919172f binds807 source/checkout inputs. Four Release suites pass
0.07s: versioned codec, receipt handoffs and capture/render regressions. Codec5,537
checks include every truncated prefix across widths, signed/unsigned extremes,
unknown header fields and success-consistency refusal. Handoff checks include
actual two producer threads/one consumer, final-publication races, exact native
results, raw-prefix retention, capacity/sequence/loss faults and credit retirement.
Runtime scheduling affects the number of empty-read assertions; it is not a
functional coverage percentage.

The first pressure test found a real implementation error: the existing SPSC
constructor parameter is an initial sequence, not requested capacity. The new lane
now enforces configured occupancy independently and uses the fixed queue correctly.
The original header/tests/failing logs are retained. Terminal publication ordering
was reviewed and strengthened, with concurrent final-publication acceptance. Initial
initializer/fixture indentation warnings are retained and corrected, not suppressed.

Four ASan/UBSan suites pass0.62s with leak checking disabled. Release and sanitizer
private PipeWire1.6.2 runs each capture96,000 frames and render96,123 frames into an
owned test sink. Capture receipts verify375cycles and96,000returned/retained frames;
render receipts verify376cycles and96,123queued monitor frames, plus133 final zero
frames. Every native cycle round-trips through the codec outside RT callbacks.
Audited callback allocation/free/lock hits are zero. The owner-only temporary socket
has a dummy driver/test nodes only, no physical devices/session manager/speaker
route/default-device/volume changes. All clients/daemons are joined; no VM is used.
These successes do not causally resolve the earlier contention gap.

The portable receipt suite passes ThreadSanitizer in0.35s with no suppressions.
Six portable capture/render/queue/codec/fixture objects compile for Windows without
warnings; SDK link/runtime qualification remains pending. Full evidence is in
[the qualification packet](../tests/results/M2/2026-10-12-pipewire-capture-returns/summary.json).
Neither compiler success nor narrow thread evidence qualifies physical/installed
routes, opaque framework ownership, GUI TSan or all frozen workflows.

## Original PR160 runtime evidence

PR160/run38304377056, which predates these receipts, passes all four jobs: Linux146
checks/360.90s, Windows desktop20/75.43s, native73/226.77s and core cross-build. All
three additional original concurrent-owner diagnostics complete20 takes. The selected
native JSON records the deliberate short-tail fixture after join (stage7), not a
recurrence of PR159's unexpected pre-cancel startup failure. Exact original job logs,
artifact members, hashes and source bindings remain retained.

This is independent success on that revision; it does not prove a root-cause fix
for PR159's44.1kHz startup wait, PR158's pool-exhaustion failure or checkpoint225's
real capture-cycle gap. No unchanged rerun replaced their original evidence. No
pacing, buffer, deadline or diagnostic failure threshold is relaxed here.

## Next implementation task

Bind these queues/codecs to the versioned project-owned PipeWire recording journal
with durable checkpoints, common-prefix recovery and explicit source/native-return
provenance. Combine genuine capture/render with the shared processing owner and
integrate the route into the native controller and Qt workflow. Keep raw recordings
separate from monitoring correction and retain explicit user timeline placement.
Continue causal investigation of startup/contention failures. Full Linux native
adapter, verified alignment, installed/physical qualification, later milestones and
reviewed European localization remain required.
