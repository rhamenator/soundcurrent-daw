# ADR 102: Supervised offline render and guarded desktop adoption

2026-10-09. Status: control component and selected-clip dialog implemented; native
qualification and preview delivery pending.

Run offline stretch in a low-priority Qt control worker with one admitted operation
and an OS-contained child. Reserve the entire child ceiling before spawn, pin/admit
the input, bound application channel banks and keep child credit through reaping.
Publish immutable snapshots. Cancellation, deadline and shutdown do not authorize
deleting jobs or inferring that publication failed from the exit code.

Independently verify any owned completion after reaping, even when termination was
ambiguous. Keep canceled/abnormal flags with completed evidence for explicit review.
Attach through a canonical guarded command which holds the verified result credit,
checks project epoch/root/ID and expected clip/raw state, and reports correlated
success or rejection. Refuse unchecked desktop payloads and foreign ledger credit.

Local acceptance includes the application-window render/review/apply workflow.
This does not establish physical/native audio,
hard parent filesystem deadlines, an OS privilege sandbox or processing-quality
parity. Existing Linux/Windows core evidence keeps its exact preceding source scope.
See [checkpoint 136](../136-stretch-supervision.md) for acceptance and continuation.
