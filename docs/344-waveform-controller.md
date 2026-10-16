# Asynchronous waveform supervision

Checkpoint 344 adds `WaveformController`, a Qt control-thread adapter around the
checkpoint 342 worker. It is not yet connected to the timeline or installed
preview. Viewport scheduling, caching and channel-aware drawing are next.

## Ownership and admission

The controller admits one operation at a time and returns a bounded `Full` or
`Closing` response for another request. A selection contains the complete source
ticket, approved-root path and relative media reference. Input names and geometry
are bounded before admission; retained selection storage has a resource lease.

A low-priority control thread owns process startup, channels, decoding and
retirement. Before launch it reserves the declared child payload allowance, the
exact expected reply bank and control/channel overhead from the shared ledger.
The decoded peak model must also obtain its own reservation while the receive
bank still exists. Child credit is released only after that child is terminal.
GUI borrowers may retain an immutable result after clear or controller shutdown;
its storage remains charged until the last owner releases it.

These reservations bound admitted owned payload. They do not impose an OS memory
limit or guarantee Qt allocator/RSS usage. The adapter uses the worker's existing
trusted geometry/format envelope; it does not add a product track-count limit.
Neither this adapter nor the worker does media-file watching or persists a GUI
path string as an already-open directory capability. The child approves and pins
the explicitly selected root/file at execution and verifies expected source bytes.

## Deadline, process and result checks

The deadline starts at request admission and covers scheduling, preparation,
startup, child work, reply decoding and final freshness checks. Expiry initiates
termination, followed by kill after a 200 ms grace interval. Cancellation, stale
selection and output overflow likewise stop the child. The controller keeps
waiting off the GUI thread until the actual process owner is terminal; it does
not promise a hard bound on OS kill/retirement latency. Destruction has a fallback
join; normal GUI integration must request shutdown and observe `closed` first.

Success requires the exact expected byte count, independently observed PID,
normal zero exit, empty stderr, valid protocol and fully consistent peaks. Only
exact known numeric diagnostics are recognized; foreign error text is never
forwarded as a trusted localized message. Truncated, extra or contradictory
output produces no waveform. A late cancel is serialized against terminal
publication, after decoding has finished.

The caller supplies a thread-safe, bounded, memory-only current-selection
provider. The controller compares the complete operation/epoch/generation/source
ticket, root and relative name before launch, during child execution and before
publication. Replacement produces `Stale` without a result. This callback must
not read GUI objects or perform I/O.

A borrowed snapshot can become stale after the final worker check. Consumers must
therefore call `resultFor(currentSelection)` at use; it returns no waveform when
the current selection differs. The future timeline/cache adapter must use this
contract at publication and drawing, and invalidate on project/source/view changes.
The controller alone cannot guarantee that a future GUI consumer follows it.

## Verification

Both Release and ASan/UBSan pass five focused suites in 2.15 and 6.55 seconds,
respectively, with one low-priority build/test worker and no VM or physical audio:

- 106 controller checks with actual child processes: Unicode source paths,
  independent source extrema, one-operation admission, bounded credits, immutable
  borrowers, clear/shutdown, cancellation before launch and after child completion,
  live cancellation/close, stale generations during preparation and publication,
  live selection removal, admission-deadline expiry before launch, running timeout,
  stdout/stderr floods, truncated replies, header corruption, exact CRLF diagnostics,
  foreign diagnostics, missing sources/helpers and resource refusal before spawn.
- A Qt event-loop heartbeat continues while a test child is held. This checks the
  controller boundary, not a finished waveform GUI or rendering performance.
- The existing 21,060 reply checks, 53 worker cases, 164,507 source-pyramid checks
  and 20,956 pinned-WAVE checks also pass. Leak detection and sanitizer error stops
  are enabled. Source files and fixture directory contents remain unchanged.

The deliberately faulty lifecycle helper is a test target, not an installed
executable or project-selectable implementation. Native Windows CI build/test
coverage is added for this controller, but has not run on the new code yet.

Separately, original CI run `38806111667` at checkpoint 343's `0fe6729` has completed
the native Windows comp desktop job successfully. Its full original log records
`desktop-comp-assembly-apply` passing in 12.09 seconds and all six job tests passing.
That validates the strengthened stale-review workflow on Windows; it does not
identify which predicate failed in the earlier combined assertion. The overall
run is still active, so retain its full terminal evidence before PR advancement.

See the [evidence manifest](../tests/results/M2/2026-10-15-waveform-controller/manifest.json).
No new dependency, project format or translation string was introduced. Full M2
visual editing, native controller qualification and the full DAW scope remain open.
