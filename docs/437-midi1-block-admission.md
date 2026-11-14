# Bounded MIDI 1.0 block admission

`admitMidi1Block` supplies a Qt-free, stateless event boundary for later live and
offline consumers. A single owner supplies an immutable ordered span of complete
`TimedMidi1Event` values and distinct, already prepared optional output slots.
Every event carries an absolute project `Frame`, a nonzero transport generation,
a nonzero engine endpoint token and an owned validated `Midi1ShortMessage`.
Endpoint tokens require a future stable-ID registry; they are not native handles
or a new serialized project identity format.

A window has a nonnegative start, one through 65,536 frames and a nonzero
generation. Its exclusive end must fit in `Frame`. The entire input is validated
before any output changes: at most 4,096 events, nondecreasing times, no late
events, exact generation and nonzero endpoint, including events beyond this block.
At most 1,024 events may fall inside the block, and the caller must provide that
many output slots. Refusal reports zero consumed/emitted events and preserves all
output slots. There is no silent truncation, timestamp clamping or stale-event
discard. Work and storage are bounded; the function allocates nothing and does
not throw, wait, log or perform I/O.

On success the function copies the input prefix in `[start, end)` to the output
prefix, replacing absolute times with exact sample offsets. Equal-time events
retain ingress order and bytes. The returned emitted count is also the consumed
input prefix length. Events at the exclusive end belong to the next block. The
caller carries the unconsumed suffix and reads only the reported output prefix;
unused output slots deliberately remain unchanged. Input/output ownership must
remain exclusive for the call. Transport discontinuities require caller-prepared
generation/timeline replacement; this function does not reset queues or voices.

Tests exercise boundary/carry semantics, equal-time ordering, integer endpoints,
maximum budgets, refusal without partial writes, invalid future metadata,
ownership and an independent interval oracle. Linux instruments C/C++ allocation
and pthread mutex entry points; Windows instruments C++ allocation only, matching
the existing audit's limits. Zero counters are not complete platform callback
safety proof.

This is not a device adapter, concurrent queue, timestamp correlation model,
note lifecycle manager or playback/recording feature. Stop/note-off reservation,
sustain/panic, seek/loop voice handling, device reconnect, stable endpoint mapping
and integration into the audio graph remain required. No MIDI or automation
parity is established by this boundary.
