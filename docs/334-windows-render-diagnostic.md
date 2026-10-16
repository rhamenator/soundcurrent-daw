# Installed Windows render timing and process observations

Checkpoint 334 observes the exact installed `b5dc256` candidate from
[checkpoint 333](333-installed-windows-progress.md). No application, helper,
installer or audio-processing limit changes. The purpose is to distinguish
elapsed time from CPU work before choosing a performance change.

## Bounded external observation

A separate C# observer uses read-only process-query handles for the actual main
process and its direct `sc-comp-span-worker.exe` children. It checks each full
executable path and creation time, retains the handles through child exit, and
records counters at a nominal 100 ms interval. Bounds are four process handles,
4,096 rows and 90 seconds. The actual run observes three processes and 519 rows;
no bound or counter query fails. It does not sample or modify audio callbacks.

[GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)
provides accumulated kernel/user CPU time in 100 ns units.
[GetProcessIoCounters](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters)
provides process I/O accounting. Those counters include operations other than
physical disk traffic; they cannot establish a disk bottleneck or identify a
particular file or security service.

The public progress controls are observed separately. Whole accessibility-tree
queries take up to **583 ms**, add unmeasured overhead, and may miss brief phases.
Stage boundaries below are observation windows, not internal function timings.
The main process's CPU/I/O totals also include GUI and accessibility-provider work.
This is a diagnostic run, not a quiet-machine benchmark.

## Observations

The application reports a completed, verified render at **36.9 seconds**. The
external driver first observes readiness at **37.527 seconds**. Both exceed the
original 30-second expectation. The wider observation window collects evidence;
it does not alter the product's deadlines or make the original expectation pass.

| Observed stage | First observation | Until next observed stage |
|---|---:|---:|
| Checking source/history | 0.732 s | 2.674 s |
| Rendering source spans | 3.406 s | 23.253 s |
| Assembling/verifying transition | 26.659 s | 6.056 s |
| Verifying transition | 32.715 s | 4.534 s |
| Complete | 37.249 s | Terminal |

| Process | Observed presence | CPU time | Read operations | Accounted bytes read |
|---|---:|---:|---:|---:|
| Main, observation-window deltas | 37.544 s | 22.1875 s | 157,779 | 1,604,298,734 |
| First worker, lifetime totals | 5.397 s | 2.6875 s | 26,846 | 572,430,976 |
| Second worker, lifetime totals | 20.576 s | 2.640625 s | 27,253 | 573,258,979 |

Both worker handles report exit zero. Their similar CPU work and accounted read
volume, with substantially different elapsed presence, justify investigating
waiting and scheduling alongside processing. They do not establish the cause.
The approximately 10.6-second assembly/verification observation window is also
material. Complete history validation is repeated at publication and verification
boundaries in `src/comp_assembly_job.cpp`; internal call timings are needed before
changing those integrity checks.

## Render correctness and original automation failure

Rendering and verification complete, the elapsed label freezes, and the two
result WAVs match the previously independently verified installed results exactly:

- `05cfa2aa050342c2aba4e6cce3588b9221703ce5dd16965d438e5e2bb7d65244`
- `ea221a995cb95bc65f4bdb8113cfc0e3806dd0151bbd28630a57af7eb314a04f`

After the timing capture, an accessibility `FindAll` call raises
`ElementNotAvailableException` during Apply. The original automation remains
**failed**. A host screenshot subsequently shows the completed Apply and unsaved
state. The host closes that review, explicitly saves and closes the actual main
window through normal keyboard controls. Saved clip timing/processing and rendered
WAV hashes match the prior reference, and all 202 original history/media files
remain unchanged. Undo/Redo, reopen and export were not reached in this run; the
[checkpoint-333 evidence](333-installed-windows-progress.md) remains their separate
qualification. The main exit code was not captured in this recovery.

The first host verifier incorrectly expected `project.previous.json` to equal the
new state, copied from the prior workflow that saved several times. This run saves
once after Apply. The corrected check confirms the previous file retains the
original tracks; it does not change the observed project or the failed driver.

Retirement finds zero owned processes, all 65 installed payload hashes unchanged,
and all 401 files inventoried across installed previews unchanged. The independent
clone shuts down; all VMs are off. The temporary receiver, firewall rule and mounted
transport are retired. No physical audio, original VM or pristine template is used.

## Required hosted CI and retained evidence

[Original run 38782533935](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38782533935)
at `1cc2db59c1f35b147414bbac4448c20e0a0d6335` passes **all 15 jobs**. This includes
the new separate retained-evidence job and the required Linux aggregate. All 15
full job logs and all 12 original artifact archives are retained; published sizes,
SHA-256 values and ZIP CRCs pass. The original archives total **1,989,818,758 bytes**
and stay local. Earlier cancelled/failed runs remain recorded separately.

Evidence: `tests/results/M2/2026-10-15-windows-render-diagnostic/acceptance.zip`,
115 entries, 3,090,492 bytes, SHA-256
`e72a9d0e2e66312ba50d0dbc63f6867160f1b9e1fdf0a5744cb5e9baa9334cf4`.
It retains the original failed workflow, process/UI samples, observer and analysis
sources, cleanup/correctness receipts, all CI logs/APIs and hash-pinned reconstruction
of the 225-file project. CRC, all payload hashes and reconstruction checks pass.
Binaries and transport credentials are excluded.

Next: obtain internal render/history-verification timings with lower observation
overhead, then optimize the demonstrated cost while retaining source identity,
cancellation, bounded resources and independent sample checks. Render responsiveness,
previous Windows findings and the full DAW/quality/hardware/localization scope remain
open. No binary release or merge is made.
