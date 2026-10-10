# ADR 103: Explicit stretch context and nominal visible crops

2026-10-10 UTC. Status: local acceptance complete; native/installed qualification pending. Frozen baseline unchanged.

Keep a raw selected span distinct from its full processing region and derived
visible crop. A user may explicitly request real neighboring source frames on
either side; do not infer context, pad an unavailable edge, or process the whole
file silently. Context counts use physical source frames, preserve the exact
fractional source origin and remain bounded by the original asset and job grants.
The original selected span remains the rerender/Undo anchor.

For an explicit region of F frames, admit T = ceil(F * n/d) output frames. The
existing offline processor receives T/F and must drain exactly T finite frames;
no repaired padding or output truncation is allowed. The visible crop follows
the nominal n/d map, including its fractional phase. This avoids changing a
128-frame selection at 3/2 into 193 frames merely because its neighboring region
has an odd length. Legacy derivatives retain their original rounded T/F map,
processor IDs, keys and samples. Loading never regenerates a derivative.

A standalone 36-render experiment compared nominal n/d with ceil-adjusted T/F.
The nominal route drained 2048 rather than the admitted 2049 frames for F=8193,
n/d=1/4. The adjusted route met all 18 exact-duration points. Therefore nominal
vendor processing cannot replace the exact drain contract based on these tests.
Using a nominal crop with the adjusted processor does not establish local event
alignment: the earlier retained transient/context counterexamples and listening
gates remain open. Explicit context is a usable, undoable workflow foundation.

New region processor IDs bind the crop semantics independently of old R3/copy
IDs. Keys include canonical nominal settings, actual context counts and crop-map
version, even when two settings round to the same full output length. Schema
1.14 persists nullable context; schemas 1.0–1.13 migrate to absent context. Helper
protocol 4 binds region mode and both counts and charges the entire region.
Current package receipts require actual region acceptance; historical protocols
2/3 are accepted only by an explicitly trusted inspection call.

Exact geometry, raw-relative rerender shifts, state validation and bounded Undo
stay outside real-time callbacks. Both live playback and offline export consume
the same verified derivative. GUI fields opt in, restore saved context and cap
counts against the retained raw source. Render never applies automatically;
Apply remains an explicit transaction, followed by audition and Undo as needed.

Next qualify musical transients, asset-edge behavior, fractional phases, grouped
channels and local timing landmarks. Then implement versioned warp maps with
measurable alignment gates. Do not infer full Q-STRETCH, native/installed parity,
foreign-property adoption or European translation delivery from this increment.
See [checkpoint 144](../144-explicit-stretch-context.md).
