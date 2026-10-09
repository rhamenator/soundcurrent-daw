# ADR095: clip processing before overlap summation

Status: selected; scoped local acceptance, new native qualification recorded separately.
Date: 2026-10-09.

| Alternative | Functionality, licensing, maintenance and integration | Decision |
|---|---|---|
| Apply clip gain after track summation | Cannot distinguish overlapping sources; wrong clip semantics | Refused |
| Separate offline-only renderer | Playback/export can diverge; doubles maintenance and quality work | Refused |
| New general audio framework for gain/fades | Additional licenses/build/state/thread ownership without demonstrated need | No dependency added |
| Stateless prepared original processor in existing shared reader | GPL-3.0-only original C++20; strict schema/history/resource integration; independent analytic/live/export qualification | Selected |

Store explicit sample-defined windows and stable machine curve identifiers, not
localized labels or unsupported claims about proprietary reference curves. Shift
signed anchors during unity-rate trim/split so the same source frame retains its
gain. Reject invalid arithmetic before assignment. Preserve raw assets and float
headroom. Place per-clip processing before overlapping voices sum and before track
EQ, shared by read-ahead playback and export.

Prepared playback snapshots/slabs currently prevent structural clip changes while
audio is prepared. Keep that explicit guard until bounded generation/voice events
and safe retirement implement immediate live clip automation. A GUI scalar alone
cannot update already prepared summed slabs correctly.

The existing Qt spin-box adaptation remains within reviewed reuse provenance;
no equalizer project changes. Exact canonical doubles survive untouched rounded
display fields. Apply is one bounded transactional history edit. Current schema1.9
migration is explicit and older versions refuse new fields.

libsamplerate, Rubber Band and soxr remain candidates for the next source/project
timing slice; checkpoint129 links official licenses/APIs, maintenance observations
and bounded Linux feasibility. No production resampler or pitch/stretch dependency
is selected by the four-tone experiment. Exact pins/transitives, processing-quality,
platform/deadline/latency and full rate/layout gates precede adoption.

## Focused processing fields and history

Native Windows reproduced a focused gain control retaining stale display/canonical
state after model Undo. Refresh compares the previous/current selected clip's
stored processing and updates the processing fields when that value changes,
including while focused. Unrelated publication retains in-progress focused input.
The actual focused Undo/Redo assertion and deadline remain intact. The desktop
repair changes neither processing, schema, resource grants nor callback ownership.
Six affected local Release and three ASan/UBSan desktop tests pass; exact-source
Windows qualification is a separate gate. Earlier hosted and large sanitizer
failures remain retained; no deadline or protection was relaxed.
