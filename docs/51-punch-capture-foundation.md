# Prepared punch capture with uninterrupted playback

Date: 2026-10-06. P004 / M2 foundation. Full punch recording remains incomplete.

## Engine contract

`DuplexRecordingOptions::punch` optionally selects a half-open **raw engine-frame**
range `[begin,end)` inside the prepared playback range. Every capture pipe's
`startFrame` must equal `begin`. Admission rejects empty/reversed/outside ranges
and mismatched lanes before creating capture pools or recording jobs. Without
this option, existing full-range capture behavior remains.

The callback validates the native block before either playback or capture. Raw
capture takes the intersection of that block and the punch range. Playback and
explicit Off/Post-EQ monitoring still process the full valid block, including
preroll/postroll. Separate prepared capture pointers handle a start inside a
block without shifting live monitoring. Raw copies precede aliased output writes.
At exact punch-out, capture pipes publish their final partial slabs and finish
with RangeComplete; playback continues. Writer completion does not destroy any
callback-owned object or bypass the owner's native-join/result policy.

The first captured sample supplies the shared capture origin. Its device position
and integer monotonic timestamp include the within-block offset; cycle and driver
delay describe the containing callback. Zero timestamp stays unknown. Timestamp
overflow faults before publishing/copying samples. Device discontinuities during
preroll/postroll still terminate playback honestly. A later stop/fault cannot
overwrite a previously completed capture's end reason.

All extra pointer storage is admitted and allocated during preparation. No new
audio allocation/free, blocking lock, disk operation, log, GUI call or queue is
introduced. Arithmetic/headroom, parameter IDs, project/journal schema, regular
checkpoint policy and worker retirement remain unchanged.

## Qualified workflows

The [fixture](../tests/punch_tests.cpp) checks five ranges across callback sizes
1/7/127/256 and a varying partition. Cases include a one-sample punch, nonaligned
boundaries, full range and either shared playback edge. An independent raw sample
oracle checks mono and mapped stereo takes, counts nonfinite input only inside
the recorded window, and retains values above full scale. A separate file/live
matrix oracle checks every output sample through preroll/postroll and silent
callback slack, with both output planes aliasing capture/monitor inputs.

Journal origin, supplied latency alignment, original media and underlying clips,
grouped undo/redo and Save/reopen are checked. This verifies the existing
`capture.startFrame - inputLatencyFrames` attachment rule; it is **not** proof of
latency-compensated musical locator geometry. A worker-owner fixture checks that
writers finish at punch-out while playback continues, with receipts only exposed
after the normal joined stop. Invalid ranges and mismatched start frames refuse
preparation. Additional checks cover positions near INT64_MAX and timestamp
overflow/unknown handling.

Interrupted capture before, inside and after the window is recovered using
actual durable checkpoints. In the postroll case, 568 frames were captured/written
but only 512 were committed at the 128-frame checkpoint threshold. The remaining
56 original frames stay untouched; recovery copies only 512. The capture end
reason is RangeComplete while that earlier journal still records Unknown. Empty
preroll never fabricates an origin or recoverable take. The first development
fixture incorrectly expected final reasons/full written length in every journal;
it was corrected after inspecting the existing checkpoint contract, without
altering any durability threshold or implementation.

## Evidence and remaining work

All Linux Debug groups pass: 29/29, 46.81 s. The Release oracle passes 25 boundary/
partition workflows and zero audited RT allocation/free/lock violations. Linux
Debug/sanitized/Release builds and the Windows headless core/tests compile and
link. Windows Qt, native devices, execution and installers remain unqualified.

The full sanitizer run passes 28/29, including punch capture. Desktop timeline
fails its combined selection/playback-preparation assertion. Original branch and
admission snapshot are unknown; no memory diagnostic is reported. Its actual
executable, full logs and exact inputs are retained. This twenty-first observation
does not establish a punch cause and is not erased by other passes. See
[evidence](../tests/results/M2/2026-10-06-punch-capture-foundation.json).

Next reproduce and fix controller-published selection before the GUI's next poll,
then implement **latency-aware musical locator preparation, persistent locator
state, desktop controls and owned native punch acceptance**. Auto monitoring,
manual punch, recorder stops/continues options, loop/take lanes/comping and audition
ownership remain required. Capturing a new clip alone does not qualify overlapping
take selection or a complete recording workflow.

The sustained native deadline/durability gate, physical/load/Windows reliability,
all other frozen professional requirements, project import, equipment profiling
and all-Europe delivery remain open. All 92 acceptance/quality/reference/F/Q/C/N
contracts are unchanged; no axis is promoted. Equalizer inputs stay read-only;
no dependency, license, host policy, publication or signing purchase is added.
