# Preserve interruption failures through cleanup

Checkpoint 328 fixes the Windows interruption observer's cleanup path. It does
not change the DAW's rendering code, worker deadline, interruption boundaries, or
recovery acceptance criteria. Native qualification of this observer change is
still required.

## What the original failure proves

The original failed desktop artifact from CI
[38766825424](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38766825424)
records an 8.709-second partial-audio wait. The attempted partial suspension was
10.839 seconds after the Python observer began launching the parent, in that
observer's own monotonic clock. The probe declares a 5,000 ms parent deadline,
and `cancel.request` was already present when suspension failed. The controller
bytes are unchanged since that run: its deadline begins before child startup,
includes the test's acknowledgment hold, and can initiate cancellation while the
observer is still waiting for partial audio.

Both process handles were nonsignaled at the recorded failure observation. No
terminal cleanup receipt survived. The failed project's media was not uploaded.
This evidence supports investigating an overlapping cancellation/termination
path; it does not prove the precise cause of `SuspendThread` error 5.

Microsoft documents that process termination is asynchronous, waits for pending
I/O to finish or cancel, and can return access denied when called on an already
terminated instance. Actual handle signaling is needed to establish termination.
That makes an exit race plausible, not proven for this run.
[TerminateProcess reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess).
The suspension remains a debugger-style operation confined to the owned test
helper, with failure preserved; it is not a product synchronization mechanism.
[SuspendThread reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-suspendthread).

## Concrete cleanup defect and repair

The original traceback confirms that a second exception from parent cleanup
replaced the primary suspension failure. Sequential cleanup also prevented the
helper cleanup and final reports from running after that exception.

The observer now:

- Attempts termination of both owned instances before bounded terminal waits.
- Uses the existing instance handles and the owned `Popen` fallback; it never
  reopens or targets a process by PID during cleanup.
- Avoids issuing a duplicate parent termination after a successful request.
- Records actual terminal exit codes separately from requested termination codes.
- Continues closing saved thread handles after a resume or close failure.
- Preserves the primary exception and adds cleanup diagnostics as exception notes.
- Fails a previously successful case if cleanup itself reports an error.
- Writes `cleanup-report.json` alongside the existing terminal and timing reports,
  including explicit incomplete-terminal observations and individual errors.

The deliberate interruption steps still use `Process.kill`, which requires the
expected owned termination code. Cleanup cannot substitute a natural exit for a
successful deliberate interruption, or turn a failed boundary into a pass.

## Verification and limits

Eight fault-injection tests exercise the actual observer cleanup functions with
mocked Windows calls: original-error preservation, parent failure with helper
retirement, cleanup-only failure, already-terminal instances, failed terminal
observation, missing-identity launcher fallback, report-write failure, all-thread
resume/close attempts, and the strict deliberate-termination exit check. Several
properties are checked in the same test. All eight pass on Linux; these are
Python control-flow checks, not native Windows process qualification.

The existing collector and observation-timing checks also pass. Python compilation,
CI YAML parsing and `git diff --check` pass. CI now runs the cleanup fault tests on
Linux and in the Windows main desktop bank. Current CI
[38773460845](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38773460845)
is still running on the previous checkpoint; this change has not replaced that
run. No VM was started and no installed preview was changed.

Evidence: `tests/results/M2/2026-10-15-interruption-cleanup/acceptance.zip`, 149,067
bytes, 13 entries, SHA-256
`e39d7ecef0f53460584eea25c9e4716d6f5e89dc455cb0c818d4a18592f10acd`.
CRC and every payload hash were verified. The archive includes the original
failed desktop artifact/log, derived timing analysis, patch and focused results.

Next: retain the currently running CI's original terminal outcome before advancing
the draft PR. Qualify the observer changes natively, using retained process and
terminal records to investigate the missed partial boundary. The original failure
remains open; changing a deadline or repeating a passing run would not resolve it.
Then continue qualification of the comp preview and the full DAW backlog.
