# Packed MIDI 1.0 capture normalization

`Midi1PackedMillisecondCapture` pairs a previously validated original millisecond
capture stamp with a supplied 32-bit legacy packed word. The low byte is status;
the following one or two bytes are data only when that status requires them. The
high byte and any unused data positions are padding. The normalizer reuses
`Midi1ShortMessage::fromBytes` as its only status/length/data validator, trying at
most three bounded prefixes. Unsupported or malformed active bytes refuse the
value. Unused padding never becomes MIDI payload, but the complete original word
is retained separately for provenance. The value has no invalid default, owns all
fields, is trivially copyable and fits within 80 bytes. The message accessor borrows
from a named capture, so its byte span remains valid for that capture's lifetime;
message access on an rvalue capture is deleted. Retaining a span after its owner is
retired remains invalid.

Original stamp identity/epoch/sequence, zero or maximum milliseconds, notice and
optional declared uncertainty are copied without projection or arithmetic. A
consumer-behind notice remains attached to the received event; it does not assert
that the supplied event was lost. Velocity-zero note-on stays unchanged. No state,
byte-stream decoder, running-status reconstruction, queue or native handle is added.
A refused value leaves caller-owned original input available for its own explicit
error/unsupported-message policy; refusal is not permission to silently discard it.

The packing contract follows Microsoft [MIM_DATA](https://learn.microsoft.com/en-us/windows/win32/multimedia/mim-data)
and [MIM_MOREDATA](https://learn.microsoft.com/en-us/windows/win32/multimedia/mim-moredata),
consulted November 14, 2026. Those notifications carry expanded short messages and
milliseconds since stream start; SysEx uses another notification path. This portable
helper does not receive callbacks or establish callback serialization, native
pointer-width extraction, endpoint registration, timestamp correlation, UMP/SysEx
support, critical delivery/panic, recording or hardware/performance parity.

Tests use an independent status-length oracle across all 256 status bytes and all
values in each possible data field, both high-byte extremes, plus all legal data
pairs for every channel-voice status. They check padding/provenance separation,
malformed-active-byte refusal, unchanged wire bytes, stamp/word ownership and
limited allocation/lock counters. Linux and actual native Windows use the shared
source and existing audit limits; zero counters are not full callback-safety proof.
