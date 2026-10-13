# Native control command payload admission

2026-10-11. M2. Full-suite scope remains active and incomplete.

## Coded change

The existing desktop recording controller now reserves command payload credit
before copying settings or preparation into its 16-entry FIFO. Accepted commands
retain their lease while queued and while the control worker handles them.
Cancellation clears queued leases; processing, refusal and shutdown retire them.
The command ledger defaults to 16 MiB and can share an aggregate parent GUI ledger.
Ledger operations and copying remain outside audio callbacks.

The API takes a const command reference so quota admission precedes the controller's
owned copy. It reports separate Full, Closing, Invalid and ResourceLimit outcomes.
Refusal publishes no partial command, receipt, native preparation or pending job.
Urgent Cancel retains its independent atomic path, including at a full quota.

Structural admission bounds EQ declarations to 64 bands, map routes and selected
port arrays to the existing 65,536-entry geometry ceiling, path units and each port
text field to 4,096. Port text must be valid UTF-8 and strings/paths cannot contain
embedded terminators. Payload arithmetic checks overflow and stops at the current
byte limit. These bounds govern transport payloads; native format/identity/parameter
semantics continue to be validated by the original worker and SDK admission.
They do not impose a new logical project-track limit.

Charges cover known command/preparation containers, copied route/band arrays,
paths and port text, with conservative storage allowances. This is payload
accounting, not an exact allocator/RSS ceiling. Callback dependencies remain
caller-owned; product preparation hooks must retain shared dependencies rather
than embed large opaque captures. Immutable published snapshots, results and
permanent panel/model storage still need their separate GUI ownership admission.
The visible panel is not enabled by this checkpoint.

## Acceptance

[Qualification packet](../tests/results/M2/2026-10-11-native-command-admission/summary.json)
binds 780 exact source/checkout inputs and retains original/corrected source epochs,
full commands/configurations/logs, object/archive/executable hashes and the first
failure. The production controller/owner archives were built from the initial
implementation; only the corrected fixture changed afterward, verified against
all other input hashes. Those archives and unchanged engine dependencies are
reused read-only; no object/archive/executable/audio payload is uploaded.

The expanded existing suite passes Release 17.32 s, ASan/UBSan 24.92 s and TSan
28.40 s. Original native/recording/EQ/failure/pressure workflows remain present.
Additional real-worker/simulated-SDK acceptance verifies:

- Queued/in-flight payload credit participates in both parent and child ledgers.
- Quota refusal preserves usage, commands and observable state.
- Oversized bands/text, malformed UTF-8 and embedded identity terminators are
  refused before copying.
- An actual callback-copy witness stays at zero when credit is unavailable;
  the factory is never reached and no pending recording is published.
- Urgent Cancel at quota invalidates queued activation and releases all command
  credit after settlement. Native callbacks are joined and guarded violations zero.

The first TSan run failed a new test's pointer-identity comparison: an idle worker
can publish an equivalent immutable snapshot at a different address. Original
output/source/commands are retained. The corrected check verifies semantic state,
factory invocations and a real copy witness; it does not relax a timing or recording
assertion. Four current Windows objects, including the genuine factory, compile
warning-free. Current admission runtime on Windows remains pending.

Qt Linguist refresh updates the two changed UI source hashes. Catalog keys,
translations and review statuses remain unchanged; catalog audit and corruption/
refusal workflows pass. All-European delivered language coverage remains open.

## Preceding candidate: original Windows timings

[PR150](https://github.com/rhamenator/soundcurrent-daw/pull/150), exact head
`f8cbc8be2300f1cf240c102072886752df420dcb`, tested merge
`30db8f49fe0f20689a721603ce5ab346706c2f3f`, run `38189943303`, passes all four jobs.
Native Windows passes all 71 suites, the original 22-case manual test and all three
original standalone 20-take/five-second diagnostics. Desktop passes all 19 suites
and all four interruption cases. Linux passes 143 suites, including the repaired
catalog audits. This executes the preceding timing/catalog code, not this new
payload admission.

The original Windows observer records a 29.407 ms acknowledgement hold and
69.061 ms partial hold for the during-processing case, including a 41.927 ms live
recovery probe. The helper subsequently terminates after a 222.697 ms observed
wait. Separate helper-death coverage records a 30.154 ms acknowledgement hold and
87.610 ms partial hold. Before-ack coverage observes the actual watchdog exit
through its 4,901.845 ms terminal wait. These are same-observer spans, not a
cross-process device/project clock mapping.

All 22 original manual cases have matched completion markers in the native
artifact, including the concurrent owner case's 6.6102544 s total across 20 takes.
Its individual original five-second waits remain unchanged. These measured passing
observations do not explain the earlier full-suite 60.11 s timeout, independent
audio-flush stall or earlier watchdog expiration. No causal fix is claimed.

[Original runtime packet](../tests/results/M2/2026-10-11-original-timing-windows-runtime/summary.json)
retains all four byte-exact original job logs, original native artifact logs,
Windows case/observation/probe/cleanup metadata, exact 813 changed blob and 780
qualified input bindings, and artifact size/hash receipts. ZIPs and their binaries/
audio remain local. No readiness or ordinary merge admission is performed here.

## Next implementation

Use the coded controller and admitted commands to build the visible finite
recording/device/EQ/result-review panel. Add ownership for its inventory, model,
snapshots and results; localize controls and qualify current Linux/Windows UI and
native execution. Then qualify hardware scheduling/alignment and installed/reopen/
export workflows. Full M2–M11/F/Q/C/N/X004/X005/platform/localization scope remains
active and incomplete.
