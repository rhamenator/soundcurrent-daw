# ADR 055: native manual port and processing-stage observations

Status: accepted diagnostic design, 2026-10-07 UTC.

Retain bounded, preallocated test-only source/recorder channel markers alongside
GNU linker stage wrappers. Keep original static processing libraries and ordinary
fixtures unchanged. Serialize only after joining callbacks/control/disk ownership.
Count actual bridge calls independently and associate stage maxima with their own
clock and invocation. CPU/wall timing includes diagnostic overhead and nested costs.

Compare exact received/source IEEE bits with an independent known-waveform oracle.
Never substitute inferred quantum offsets for configured input latency. Retain
unavailable source buffers and incomplete coverage explicitly; every advancing
recorder callback still requires complete corresponding generated/received buffers.
Source startup rows that nobody consumes are not recording failures. Drops and
identity exhaustion refuse complete trace evidence.

Historical failures cannot acquire missing original terms from a later passing run.
Preserve original 41 before fixing the startup classification. The earlier delayed
channel and CPU-overrun causes remain unresolved. Finite native hash recovery can
be qualified independently without claiming sustained safety, full recording UI,
Windows qualification, parity or track scalability. See [contract](../69-native-manual-port-tracing.md).
