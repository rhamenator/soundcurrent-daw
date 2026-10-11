# ADR132: retain a bounded actual monitor tail during clock startup

2026-10-11. Accepted for prepared processing; native factory activation pending.

Trustworthy unseeded clock fitting and whole-native-FIFO output admission need
more startup time/capacity than one normal monitor packet. Retaining the entire
clock-span PCM backlog can exhaust the ordinary ring, especially at high channel
counts/rates.

Use an optional prepared recent-tail ring with original packet descriptors,
explicit trim/transfer/abandon disposition, unchanged raw capture and unchanged
clock confidence gates. Admit the source demand and output bank against the actual
native FIFO and fill sizes. Transfer only after a ready original proposal has
been committed. Record content readiness only after whole-FIFO publication.

Keep all prepared work bounded and all storage/credit off RT. Existing callers
without the option retain their behavior. Statistics remain processing-owner
state; a later explicit atomic handoff will authorize native content.

Do not seed fit history, lower confidence, allocate two seconds of source PCM,
equate a small fill with a full FIFO, or mask live underflow with silence.

Consequences: monitor preroll trim is intentional and visible; raw recording
retains the entire input. Native warmup leases, state migration, Stop during warmup,
actual SDK/installed qualification and professional latency remain required.
No dependency, driver, GUI, language-support or processing-quality claim is added.

[Checkpoint200](../200-monitor-startup-preroll.md) contains acceptance evidence.
