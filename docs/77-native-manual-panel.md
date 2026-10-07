# Native manual recording desktop qualification

This checkpoint drives the actual Qt `StudioWindow` and serialized manual worker
through owned PipeWire sources/sinks. It adds an opt-in native fixture and corrects
our adapter's interpretation of a legitimate native silence buffer. It does not
qualify sustained recording, physical interfaces, Windows runtime or full DAW parity.

## Native silence contract and original evidence

Observation62 retains the original source, executable, project, journals, callback
clocks and logs before diagnosis. Its sink's first invalid acquisition is input0:
256 requested frames, one data plane, aligned readable backing, maxsize32768,
offset0, size32768, stride4, chunk flags2, native IO `HAVE_DATA`. All lease returns
succeeded. Our old input check accepted only flags0, so it permanently faulted on
this flag before the monitor began. Earlier observations lack this metadata;
their similar symptoms do not establish that they had the same cause.

PipeWire1.6.2 defines `SPA_CHUNK_FLAG_EMPTY` as neutral media, including audio
silence ([official source](https://github.com/PipeWire/pipewire/blob/1.6.2/spa/include/spa/buffer/buffer.h)).
The adapter now retains the certified native lease and returns a distinct internal
`Silence` result with no exposed sample pointer. A filter with inputs allocates and
touches one immutable zero plane during preparation, sized to its admitted maximum
native quantum (at most256KiB). Input channels may share that read-only plane.
Callbacks do no allocation, clearing, resizing or destruction for this case.

Backing/extent/alignment/readability checks still apply. Empty size0 is accepted
only with the legitimate neutral flag and otherwise valid backing/chunk metadata.
Short ordinary data, corrupted/unknown flag combinations, invalid offsets/stride,
missing backing and failed lease returns retain their existing refusal behavior.
Input metadata and stale backing bytes are never rewritten or read as neutral audio.
One native dequeue remains paired with one return. Public project/journal schemas,
DSP math, rate/quantum, hardware defaults and recording policies are unchanged.

The new deterministic test fails against the retained original acquisition source
and passes against the correction. That isolated replay uses the current header's
appended internal enum value; it is not an original native executable. Tests retain
sentinel backing values and check lease/metadata preservation and zero callback
allocation/free/blocking locks, including corrupted+empty and unknown flags.

## Desktop acceptance

`sc-pipewire-manual-panel-fixture NEW_FOLDER MODE ARMS` is explicitly invoked,
not an implicit hardware or hosted-CI audio test. Modes are `workflow`, `cancel`,
`port-loss`, `close`; arms are3 or32. All node names are unique. The wrapper delegates
the same native endpoint used by the production default factory and observes its
worker-side receipts. Fixture peer activation occurs after all explicit links exist.
The GUI, project controller, media writers, EQ and native adapter remain real.

The repeated workflow has one existing file track, a saved stereo matrix and three
multi-lane take groups on one graph/generation. Different hash-derived channel
signals, a permuted input mapping and declared0/41/200-frame input latencies catch
swaps, offsets and stale blocks. It verifies every raw sample and every timing-origin
field against the original callback clocks and offsets. Off/Post-EQ monitoring,
live EQ edits and parameter Undo use actual audio acknowledgement positions.
A separately prepared127-frame offline EQ replay plus a float64 matrix predicts
all monitor samples; floating-point headroom is retained. This reuses qualified EQ
math and is not an independent new filter-design quality qualification.

The test explicitly adds groups, forces a verification failure by holding one owned
file, retries, exercises group Undo/Redo and saves/reopens canonical state. Cancel
retains verified committed prefixes and disables both adoption actions. Input loss
and Close retain raw prefixes; Close requires an explicit Keep choice before Save.
The input-loss case does not claim a complete post-disconnection monitor oracle.
Each generated source channel publishes its exact final generated device position.
Raw samples before it must match the marker; valid neutral input received between
source shutdown and device-loss notification must be exact zeros. Every committed
sample is checked, and the neutral count is explicit. Observation65's original
source-end bound was unlogged, so its raw-tail mismatch remains a separate gap.
The fixture does not test physical display interaction, late held finalization,
process-kill recovery, indefinite transport, loop/seek/comping or Windows endpoints.

## Exact transport prefix and teardown

The output oracle covers every frame from the first advancing native callback to
the position after native join. The independent sink must contain that entire
continuous prefix. Any unavailable/malformed/skipped callback inside it fails.

Stopping/destroying the deliberately owned producer removes its sink buffers and
may reschedule the now-disconnected sink. An empty-view rejection, a clock skip,
or both at a successor **after the complete joined prefix** is recorded separately
as teardown. Only observed masks18/128/146, unchanged clock identity/rate and no
xrun/discontinuity are admitted outside the prefix; an incomplete prefix still
fails. Extra sink samples are retained in the original file, never erased. This
boundary is not permission to ignore active-device clock gaps or shorten a failed
live recording arbitrarily. Observations53/58/63 retain the earlier overbroad
assertions and original failures rather than relabeling them successful runs.

Cancel likewise guarantees a durable checkpoint, not every byte written before
cancel. Observation52 retains the incorrect whole-written-extent assertion;
subsequent tests verify every committed sample and require a nonempty checkpoint.
No recording durability rule was weakened.

## Evidence and remaining work

The [dated M2 receipt](../tests/results/M2/2026-10-07-native-manual-desktop.json)
and evidence archive list each original and qualified run,
source/executable hashes, clocks, gate results and routing checks. Private host
routing inventories and executable bytes remain local; public artifacts contain
generated evidence and source, with independent archive verification. Timing
retains all admitted samples, thread CPU/resource coverage and the existing
p999<60%/maximum<80% period gates. Diagnostic failures remain visible. Passing a
short run does not resolve historical48's CPU cause or the longer native failures.

The final release source passes five owned-native workflows: three/32-input
repeated groups, Cancel, source removal and joined Close. The32-input run verifies
2,433,024 raw and275,456 output samples, with exact output equality. Debug40/40
and three affected ASan/UBSan/LSan tests pass. A separate actual native sanitizer
run (observation71) fails during active playback on a512-frame clock skip after
30,464 processed frames, detected by both owner and sink. No invalid acquisition
occurs and no sanitizer memory diagnostic is reported. The source callback on
cycle12385495, the first omitted cycle, took11,065,691ns wall and11,061,072ns
thread CPU (207% of its256-frame/48kHz period). Its measured resource delta has
no page faults or context switches, with10,200,000ns user and745,000ns system
time; those coarser resource counters are not a replacement for thread CPU timing.
The owner's maximum callback was1,842,711ns wall and1,835,903ns CPU and passed
the finite gate; the source failed it. This locates a source CPU outlier immediately
before the detected gap. Why it spiked remains unresolved; no scheduling,
sanitizer-overhead or historical48 cause is established. This is not admitted as
teardown or a passing native sanitizer workflow. Original
routing snapshots were not captured in that inline run and stay unavailable.
The original launch incorrectly described it as outside the observation count;
its unchanged original metadata and the explicit correction are retained.
All71 native observations, including successful runs and diagnostic failures,
remain visible. Hosted CI verifies the retained archive and original53's exact
147,000-sample raw and207,872-sample output prefixes without replaying native audio.

All92 frozen contracts remain unpromoted. X004 imports, X005 profile integration,
X006 resource-admitted tracks above the present256 ceiling, all-Europe localization
and independent Windows functional/native qualification remain required.

Next: qualify the retained original neutral-buffer regression and transport-prefix
archives in CI. Diagnose71 with bounded source generation/acquisition/return stage
timing, preserving its original evidence and unchanged deadline gates. Implement coordinated
X006 model/parser/mix admission and UI/media scaling. Independently implement and
qualify the native Windows recording endpoint; the current cross-build is not
runtime parity. Continue the full frozen roadmap and sustained recording gates.
