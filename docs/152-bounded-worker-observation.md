# Bounded render-worker acceptance observation

2026-10-10 UTC. Full-suite goal and SC-DAW-BASELINE-2026-10-05 remain unchanged.
This follows [checkpoint151](151-installed-protected-preview.md).

## Observability correction

The acceptance harness previously waited synchronously for the first stdout line,
then read pipes serially or used a second reader. A refusal with empty stderr lost
its actual exit/stdout in the v4 failure message. A blocked/inherited pipe could
prevent a test observation from reaching its timeout or obscure cleanup outcomes.
These are test-harness shortcomings; the Windows v5 failure cause remains unknown.

Both v4 and v5 harnesses now use `tests/worker_observation.py`. It concurrently
reads stdout/stderr and writes the bounded request, under one observation deadline.
A bounded queue and four-MiB combined capture limit prevent output floods. The
ready acknowledgment checks the owned operation ID before creating its start file.
Failures include actual PID, exit and hexadecimal status, elapsed observation time,
stdout/stderr, timeout/output-limit flags and uncertain retirement. V5 failures
include the exact request. A missing terminal exit remains null.

On refusal the harness retires its direct worker and observes termination. It does
not synchronously close a pipe held by a still-blocked reader: that close acquires
the buffered read lock and can block even after the direct child exited. Such a
case remains a failed observation. The helper belongs to acceptance infrastructure;
no audio callback, DSP algorithm or desktop behavior changes.

Six actual disposable-process tests cover empty-stderr failure, full stderr pipe,
ready/start handshake, blocked input, excessive output, and a direct child exiting
while another owned process holds its output pipe. The latter's owned descendant
is retired after the observation. Linux CTest and native Windows CI include them.
Both real render banks pass locally through the new observer.

## Native observations and limits

[Retained observations](../tests/results/M2/2026-10-10-windows-bounded-observation/README.md)
record the existing exact `8b46e897` Windows build. The single v4 request and full
v4 bank passed, and the default-style app opened/closed normally, exiting 0.
The v5 bank still timed out with its previous reader, without a completed report.
No new product binary was built or uploaded. All VMs are stopped.

The historical receipts retain their historical source identity. Neither the
successful startup nor reboot is evidence of a resolved underlying cause. This
checkpoint does not qualify the new source head's native bank, the refreshed
installer, physical audio, broad stretch quality, F/Q/C/N completion or European
language coverage.

## Next implementation task

Run the new observer against one failing protected native request, retaining its
exact source/binary identities, ready/completion/refusal events and direct-process
terminal independently of pipe EOF. Resolve any reproducible process or protocol
failure, then refresh the native v4/v5/Qt receipts and installed protected Windows
workflow. Parent-owned persisted rendered-job recovery follows preview refresh.
All remaining full-suite milestones stay in scope.
