# Durable checkpoint precondition in duplex recovery qualification

The positive-prefix cancellation fixture must establish a published checkpoint on
every disk owner before requesting cancellation. Advancing the playback position
does not prove those independent owners have published their journals. A worker
can correctly observe cancellation before its first journal publication, leaving
an inactive zero-frame journal with no capture origin to recover.

The fixture now observes `JournalPublishEnd` on each writer through the existing
off-thread instrumentation and waits, with a ten-second deadline, for a positive
committed count on every lane. All original positive-prefix, timing-origin,
independent raw PCM, recovery provenance and canonical-project assertions remain.
The original capture extent and injected second-writer disk failure are unchanged.
Failure diagnostics distinguish committed/written/captured counts, inactive lease
confirmation and timing-origin equality without logging from audio callbacks.

A separate deterministic case injects cancellation before the first nonempty
journal publication. It requires a zero committed count, an inactive lease, no
fabricated origin or finalized media, refusal to recover an empty take, and
unchanged journal/project state. Cancellation and production recovery behavior
are unchanged.

The previous composite assertion is reproduced by injecting that same boundary
refusal into the old positive-prefix fixture: lane 2 has zero committed frames
while capture has accepted input. Ordinary local repetitions did not reproduce
the hosted failure. This demonstrates the fixture's missing precondition without
claiming that the historical composite failure uniquely identifies this branch.
Original hosted logs and the failed shard archive are retained.

Linux CI retains the existing duplex owner test. Windows native recording CI now
explicitly builds and selects it as well. Exact-head CI and review are required
before integration; no installed preview or physical audio claim follows.
