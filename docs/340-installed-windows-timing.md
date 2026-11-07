# Installed Windows render timing and complete editing workflow

Checkpoint 340 qualifies the original 30-second observation condition for the
existing Windows preview from source
`97e9ecdc32cfaddca03e4c8df9829fbdcb68c0aa`, sequence `20261015192606`.
The application, installer and fixture are unchanged. No installer was rerun.
The [previous observer failure and continuation](339-installed-windows-batched.md)
remain separate retained outcomes.

## Observer repair

Save and render publishes its modal dialog asynchronously. The observer now
collects the elapsed label, status and review-button state from one UI Automation
enumeration. An absent initial dialog consumes the original stopwatch budget;
it no longer causes an immediate missing-control failure. Duplicate targets and
unexpected UI Automation failures still fail the test.

The stopwatch starts before invoking the action. Completion counts only when all
three controls are present and review is enabled within **30,000 ms**. Polling does
not reset that clock. At most 128 observation records are retained. Downstream
Review, Apply and export checks keep their existing assertions and time limits.

## Actual installed result

One complete run in the independent Windows acceptance clone passes:

- One initial observation finds the dialog absent; later observations find it.
- Ready-for-review is observed at **20,481 ms**; the subsequent receipt records
  20,487 ms after writing that observation. Both are inside the original budget.
- The application's terminal label reads **20.0 seconds** and remains frozen.
- Review/Apply, exact Undo/Redo, save/reopen, export and both normal closes pass.
- The independent mix calculation matches **all 18,032 stereo frames at 48 kHz**
  exactly, with maximum sample error zero and float peak 5.5167012214660645.
- Export SHA-256 remains
  `5a88856659abc1fdebb48ebdb452d6948fa9e5bfaffc898e2d7cf3dfeb9bcbac`.

Both new span requests use the installed Windows worker and regenerate the original
Linux-created history. All 202 original history/media files in the test copy and
the complete 204-file Linux source fixture remain unchanged. This is one qualified
fixture observation, not a general latency or processing-quality guarantee.
Checkpoint 339's different app-reported time is retained without treating either
run as a representative benchmark.

## Installation preservation and cleanup

All 65 installed payload files, the Desktop and Start menu shortcuts, and the
uninstall registration verify before the workflow. The existing runtime remains
`v14.51.36247.00`; no compiler or Qt SDK is on the test PATH.

Retirement confirms zero owned application processes, unchanged hashes for all
**468 files across the installed previews**, and **268 previous-workflow files**.
The latter include checkpoint 339's failure and successful continuation; transport
credentials and its duplicate input ZIP are excluded from that inventory.

The clone used reduced CPU and I/O priority and shut down normally. All VMs are
off. The temporary authenticated receiver, restricted firewall rule and attached
media are removed. Original VMs and the pristine template were untouched, and no
physical audio route was activated.

## Evidence and remaining scope

The [evidence manifest](../tests/results/M2/2026-10-15-installed-windows-timing/manifest.json)
binds a 151-entry archive containing the actual driver, observation timeline,
setup/workflow/retirement receipts, screenshots, independent verifier and
reconstructible 225-file project. Payload hashes, ZIP CRC and reconstructed project
bytes verify. Application binaries and transport credentials are excluded.

The original 30-second installed timing gate passes for this run. Earlier failed
observations are not relabeled. The original interruption and truncated protected-warp
failures still lack causal resolution; later passing runs do not explain them.
Sustained hardware, processing quality, all-Europe localization and the complete
DAW milestones remain open.

Hosted [CI 38797374241](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38797374241)
at `cae1cb1` subsequently passes all **15 jobs**. All original full logs and **12
published artifact archives** are retained, with published size/SHA-256 and ZIP CRC
verification. The original archives total 1,989,863,221 bytes and stay local; the
public evidence includes full logs, metadata and the 21 native interruption reports.

Those four native interruption cases retain the intended recovery states:
before acknowledgment and deliberate helper death are Incomplete; continuing
processing and completed publication are Ready. Each cleanup report observes all
owned terminal handles with no cleanup errors. The two partial-boundary waits are
51.941 and 17.667 ms; suspension begins 276.150 and 209.629 ms after parent launch.
This qualifies successful native cleanup and shows limited non-reproduction of
the earlier 8.709-second wait. It does not establish that failure's cause.

Next make the M2 timeline useful for visual audio editing: add bounded, asynchronous
waveform preparation and viewport drawing using the existing approved-media and
resource-accounting infrastructure. Keep disk reads out of painting and audio
callbacks, preserve stable media/source-time identity and test stale-result
rejection, cancellation, missing media, zoom and multichannel behavior. Existing
interruption, quality and hardware gaps remain tracked independently. No merge or
binary release is made.
