# MIDI 1.0 short-message boundary

`Midi1ShortMessage` is an immutable, four-byte, Qt-free value for a complete
short MIDI 1.0 message. Construction accepts exactly one defined channel voice,
system common or system real-time message of the required length, with seven-bit
data bytes. It owns and preserves the supplied bytes, including velocity-zero
note-on and channel-mode controller values; it does not execute their meaning.
`channelIndex()` returns a protocol index from zero through fifteen for channel
messages and no value for system messages.

The status and length rules follow the MIDI Association's
[message summary](https://midi.org/summary-of-midi-1-0-messages), consulted
November 14, 2026. Reserved statuses, data-only running-status fragments,
SysEx start/end and arbitrary byte streams are outside this boundary and return
no value. A future stream adapter must assemble messages and separate interleaved
real-time bytes before admission. This is not a UMP or Standard MIDI File decoder;
in particular, live status FF denotes Reset, not an SMF meta-event prefix.

Construction and access have fixed storage, bounded work, no exceptions and no
allocation, locks or I/O. Tests cover every status and supplied length, every byte
in each data position, every legal pair of data values, source-buffer independence,
constant evaluation and the runtime allocation/lock audit.

This provides a message representation for subsequent shared controller and
sample-timed event work. It does not add ports, recording, device reconnect,
timing, routing, note lifecycle, SysEx, MIDI 2.0, expressive editing or MIDI parity.
The existing event queue and EQ timing contracts remain separate. Native Linux
and Windows execution and all ordinary integration gates remain required.
