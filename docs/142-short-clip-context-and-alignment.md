# Short-clip context and event alignment

2026-10-10 UTC, frozen SC-DAW-BASELINE-2026-10-05. These bounded Linux experiments
investigate the short-span gap in [the stretch worker](134-isolated-stretch-worker.md).
They do not change its processor, qualify automatic context, or establish full
processing quality. Neither used a VM or an audio device.

## Source and timing contract

The library's [integration notes](https://breakfastquay.com/rubberband/integration.html)
explain that local input/output rates can vary around features even at a constant
overall ratio. Its pinned [4.0.0 key-frame API](https://github.com/breakfastquay/rubberband/blob/v4.0.0/rubberband/RubberBandStretcher.h)
offers internal source/output anchors; overall duration must still be supplied
separately. A global duration multiplier alone therefore does not demonstrate
local transient alignment. This is an inference from the API, confirmed by the
selected comparisons below; it is not a quality ranking of complete algorithms.

Both experiments use original generated stereo impulses with distinct channel
values, a 32,768-frame 48 kHz source, a selected 128-frame interior span at frame
16,320, and an impulse 64 frames into that span. The context render includes
4,096 real source frames on either side. Integral and half-frame origins use the
same prepared positioned resampler as the product. Duration/pitch points are
1/1 and 0; 3/2 and 0; 3/2 and +7.00007 semitones; 1/4 and -24; 4/1 and +24.
All output crops use exact rational integer coordinates at these selected points.
No synthetic zero padding, truncation of vendor output, or full-source processing
was silently introduced into the application.

## Actual existing-worker experiment

The unchanged f9a6353 Linux helper with SHA256
`faad9539d1fc1bb59f9e732630c2eedfaffbc6c5bcfa3a104b374b68bf6c4f15`
ran 30 actual child processes. All ten short spans refused before creating an
operation directory; twenty context/whole-source renders completed with exact
durations and finite PCM. Raw source bytes remained unchanged. The complete run
took 5.337 seconds; the harness used at most 58,840 KiB RSS and no swaps.

Context and corresponding whole-source crops differed at nine of ten points.
The largest selected sample difference was 0.4873982295393944 at 1/4, -24
semitones with an integral origin. The 4/1, +24 integral point was exactly equal.
This does not make the whole-source result acoustic ground truth, nor does one
equal point establish context independence. The existing minimum-span refusal
must remain until a new processing-region/alignment contract is implemented.

## Explicit-anchor and engine comparison

The standalone C++20 [probe](../experiments/stretch-alignment/probe.cpp) reused
the existing Release positioned-resampling, session and pinned Rubber Band
libraries. It tested R2 and R3, an absent or explicit impulse-position anchor,
both source origins and all five settings: 40 cases and 120 actual renders,
including 97- versus 512-frame input partitioning. Offline, channels-together,
preserved-formant and high-quality-pitch options were held explicit. These options
are experimental and do not replace the production processor.

Actual PID 2101554 exited 0 after 30.312 seconds under a 512 MiB address-space
ceiling and 60-second timeout. Peak RSS was 55,436 KiB; swaps and major faults
were zero. All renders drained to exact lengths with finite samples. Results:

| Group | Largest context/whole crop difference | Largest unity/prepared-source difference |
| --- | ---: | ---: |
| R2, absent anchor | 0.1371700168 | 0.0000235140 |
| R2, explicit anchor | 1.4872574769 | 1.4933632039 |
| R3, absent anchor | 0.4873982295 | 0.0021937371 |
| R3, explicit anchor | 0.4873982295 | 0.0021937371 |

36 of 40 partition comparisons were exactly equal; the largest remaining sample
difference was 0.002604961395263672. Vendor stderr retained 96 draining warnings
and six ignored-key-frame messages. Supplying an anchor did not establish the
required alignment or quality at these points. Peak positions are diagnostic
observations; after pitch processing they are not a universal definition of
audible event timing. The supplied single-anchor profile and these impulses do
not represent all valid warp maps or music material.

R3's pinned implementation divides by the first map key when initializing its
ratio; this probe omits the redundant zero-origin point. The future application
adapter must normalize implicit endpoints and validate exact mapping bounds
before using the vendor API, without modifying the upstream source.

## Retained evidence and next implementation

[The retained folder](../tests/results/M2/2026-10-10-short-span-context/)
contains the exact executed Python/CPP harnesses, generated raw and rendered PCM
for the worker comparison, native process results, original warnings, build logs,
library/executable hashes and archive manifests. The C++ probe retains metrics,
not all 120 output waveforms. Neither receipt authenticates untrusted data or
qualifies installed behavior. The historical worker/library bytes remain tied to
their recorded hashes, independently of later source edits.

To build the standalone probe against a qualified Release build:

```sh
cmake -S experiments/stretch-alignment -B build-alignment -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSC_DAW_BUILD=/absolute/path/to/qualified/build
cmake --build build-alignment --parallel 2
```

Next implement a separately identified unity render path: duration 1/1 and pitch
zero must preserve the prepared source, including exact integer copies, fractional
origins, float headroom and one-frame spans. Persist processor identity explicitly
so existing R3-derived assets and keys stay valid. Then implement a versioned
explicit processing region versus visible clip crop, with source/context keys,
exact output mapping and shared playback/export artifacts. Dynamic warp maps,
segmented pitch, transient/formant/spatial quality, asset-edge policies, Windows
native qualification and full frozen F/Q/C/N requirements remain open.
