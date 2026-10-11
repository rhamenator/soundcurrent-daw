# ADR126: deliver monitoring Stop intent to its processing owner

Status: accepted for the portable component; native activation remains gated.

Direct UI calls to finite-end reservation would race processing. Stop intent and
its outcome also cannot be silently lost when either owner falls behind.

Use two fixed SPSC queues with one serialized control owner and one processing
owner. Bind the handoff at admission to a run generation. Preserve original
request IDs, kinds and explicit monitoring ordinals. Consume one request only
after reserving reply capacity; receipt pressure pauses PCM progress. Maintain
route/time/clock-age validation during this pause. Never allocate/grow a queue,
drop an accepted intent or infer original capture/project alignment.

Return explicit original-request receipts for acceptance/refusal. Submission
admission and Planned are separate from native Stop/drain and finished audio.
Generation-valid ordered identifiers are consumed even on refusal and never
wrap. Terminal owners can drain refusal receipts. After all owners join, retain
unhandled/lost-reply dispositions and unread replies before retirement.

The additional fixed handoff storage receives its own resource ledger charge.
Original GPL C++20 implementation needs no new library or schema. Keep the
native duplex factory disabled until actual postroll/pressure admission and
capture/render lifecycle coordination pass native and desktop gates.

[Checkpoint194](../194-monitor-stop-handoff.md) records Linux Release, ASan/UBSan,
ThreadSanitizer, RT audits, actual concurrent queues, finite output and refusal
evidence. Component qualification does not establish native or full-product parity.
