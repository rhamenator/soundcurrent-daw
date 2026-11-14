# ADR 170: desktop assembly ownership

Status: accepted background service; main-window wiring and audio adoption open.

Use a dedicated Qt control thread with one bounded queued operation to call the
framework-independent assembly library. Require the request and parent selection
references to be present in both the canonical owner and its saved project before
publication or review. Use the owner's shared resource ledger for loading,
rendering and retained verification results.

Check root, project epoch, closing/open/create state and current processing and
reference metadata before work, at publication stage boundaries and before
exposing a current completion. Reopening the same path and project ID still changes
the epoch. Cache validation only for an unchanged immutable session pointer.
Edits may be observed after the current file finishes; they must prevent current
completion, and final audio adoption still needs its own fresh canonical guard.

Only absent jobs may start publication. Complete jobs are verified and reused;
active, interrupted, canceled or invalid jobs require explicit recovery. Never
silently resume, replace, clean up or adopt them. Persist cancellation from the
control thread against the exact original intent. A valid paired completion wins
over a late cancellation marker. On cancellation or close, do not start another
full PCM review; a later explicit review can recover the retained job.

Normal GUI shutdown requests cancellation and polls the closed snapshot. The
destructor joins as a fallback. This is a cooperative disk/control service; it
has no process isolation, OS address-space ceiling, hard deadline or guaranteed
bounded shutdown against a stuck OS file operation. Keep those limitations
explicit and evaluate a process wrapper if the later workflow needs stronger
failure isolation. No disk, mutex or Qt work enters an audio callback.
