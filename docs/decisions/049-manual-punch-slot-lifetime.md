# ADR 049: runtime punch slots preserve the active playback graph

Date: 2026-10-06. Status: accepted engine implementation; native/product gates open.

## Decision

Add a framework-independent manual punch owner beside the existing fixed-range
owner. Keep one prepared mix/read generation and continuously running EQ while
logical recording starts/stops. Use fresh one-shot capture pools per take, checked
per-lane B+L/E+L windows, reliable bounded command replies and release-published
slot retirement. Join/reclaim/replenish on control after audio drops all references.
Admit aggregate run/bridge/outstanding-take payload before pool allocations.

Overlapping postroll belongs to distinct still-active slots. A slot retires only
when every lane finishes, including lanes already finished in an earlier callback.
Control-owned slot storage is never read by audio: immutable queued pointers hold
pending-start lifetime credits; audio maintains its own fixed active references.
A retired publication is distinct from disk completion and grouped project adoption.

## Consequences

Commands use FIFO sample-frame order with explicit generation/frame/state rejection.
At most 64 accepted/unacknowledged commands and eight outstanding take slots exist.
These are resource bounds, not a total-take limit. The finite prepared generation
reserves latency postroll and closes an open take at its last admissible logical
frame. Native scheduling, indefinite transport/loop/seek, cancellation/quantization
and user-facing control policies remain separate required work.

The API documents external disk-consumer joins before slot reclamation; the next
production disk/group owner must enforce that obligation. Replacing/resetting the
playback graph per take would reset EQ/read state and interrupt playback. Recording
all preroll then reconstructing takes would change capture semantics. A lossy meter
queue cannot acknowledge recording commands or prove safe object retirement.

No native/desktop/Windows parity follows from this engine implementation. Keep
those acceptance gates and the full frozen-reference scope open.
