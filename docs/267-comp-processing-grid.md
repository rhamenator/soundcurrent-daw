# Exact fractional input grids for comp rendering

Checkpoint 267 adds an engine timing primitive and retains actual processing
experiments needed for the general comp-handle producer. Production stretched or
pitched handle generation and desktop integration remain open.

`SourceFrameMap::processingGrid(origin, span, frames)` supplies an exact raw step
of `span/frames` to the existing positioned resampler. Its exclusive endpoint is
the original origin plus the exact interval, including fractional endpoints.
It admits reduced 32-bit step components over the kernel's existing effective
range, refuses overflow, and preserves the clip playback parameter model. Split
and translated maps retain exact phase; partitions produce identical FIR samples.

The independent Fraction oracle checks 24 grids and 144 source coordinates,
including component sizes above the clip's 4,000-component playback limit. Owned
sine cases check phase/amplitude and partition independence. Seven selected Linux
tests pass in 10.84 seconds, including existing positioned kernels, fractional and
linked-rate timing oracles, and comp planning. The new core tests are also wired
into native Windows CI. Original run 38564240790 against be91b084 passed both
explicit grid gates and all four jobs; see the retained review in checkpoint 268.

## Processing observations

The [source-bound archive](../tests/results/M2/2026-10-14-comp-processing-grid/index.json)
binds implementation `01f28cc4322ef115927ecc3cdcd19799e801eb95` to source/binary
hashes, actual test/render/analysis process IDs, all owned PCM and observations.
It contains 14 offline renders and independent spectral analysis. The archive is
1,643,502 bytes with 27 entries. A dedicated low-priority build and serial worker
leave earlier qualification trees intact.

For exact raw interval `D`, processing frame count `N` and output frame count `O`,
the prepared grid step is `D/N`, backend duration ratio is `O/N`, and backend pitch
factor is the requested pitch factor divided by the grid step. This compensates
the pitch change introduced by resampling the input grid. It supplies exact
represented endpoints; it does not promise exact internal R3 landmark alignment.

The coarse test grid demonstrates the compensation: an unshifted 997 Hz input
appears near 886 Hz without compensation and near 997 Hz with compensation.
The maximum compensated spectral frequency error across both grids, channels and
three pitches is 0.34124%. One right-channel positive-pitch case remains about
8.71 Hz above its requested 2551.63 Hz. The experiment's 0.5% refusal is a
feasibility guard, not an adopted professional processing-quality threshold.
The pinned offline R3 API does not support changing its pitch method through the
realtime-only pitch quality flags. Formant compensation remains unqualified.

A full rerender of a 12,288-frame nominal stereo artifact with 26 additional
output frames at each edge changes all 24,576 nominal samples. The maximum
sample difference is 5.08837 with float headroom preserved. Independent PCM
comparison reproduces this observation. A whole rerender therefore fails the
current inner-PCM preservation contract for this case. Retain the original nominal
buffer when designing new edge generation and qualify its seam/phase behavior.

## Remaining work

Implement the supervised handle producer with separately generated edges,
explicit source-grid/protected-map identities and preservation of the current
and original inner buffers. Measure seam continuity, phase, transients, pitch and
formants with broader owned signals before admitting artifacts. Variable maps
need exact per-interval handling; integer key-frame indices alone cannot represent
arbitrary fractional raw landmarks. Paired regeneration, retained job recovery,
controller root/epoch and cancellation, desktop controls, subsequent extensions
and linked-rate/layout cases remain required.

The first experiment compilation omitted its path helper; that failure and the
later deprecated-path warning remain in the local build logs. The final probe
uses a C++20 UTF-8 path and builds without those diagnostics. Existing unrelated
mixing warnings and upstream CMake deprecation diagnostics remain visible.
The full DAW, native installed delivery and all-Europe language requirements
remain active.
