# Concurrent startup readiness and CI scheduling

2026-10-15. Source `37683240b009d12e0a4facb7669383b2501b687d`. Full DAW scope remains incomplete.

## Startup interleaving

A native callback samples independent time before it enters the prepared monitor
consumer. During that interval, the processing owner can publish valid readiness
with a later timestamp. Previously both consumers treated that timestamp as an
invalid receipt, failing before delivering any PCM. New deterministic tests
publish between the caller's time sample and the consumer's receipt acquisition.
Both existing implementations fail these tests before correction.

The corrected consumers still validate producer identity, ownership, frame count,
EOF, untouched output and terminal state. If the receipt is newer than the caller's
time sample, they retain silence/warmup until a subsequent callback supplies a
fresh independent clock. They do not rewrite the receipt, substitute a device
clock, consume PCM, extend the deadline or invent completion. Malformed finite
receipts and terminal states remain refusals; a permanently future timestamp
remains bounded by existing warmup frame/time budgets. Accepted finite output
retains exact headroom and final silent padding.

PipeWire also reads bank counters **after** acquiring the ready receipt. The
producer writes PCM/counters/EOF before publishing readiness with release; that
acquire must precede dependent observations. A counter read before the acquire
can describe an earlier state and falsely contradict a newly published receipt.
This ordering correction is supported by code/memory-order review; the current
deterministic runtime regression forces the caller-clock interleaving, not a
preemption inside the bank counter read.

The original failed Windows synthetic owner run remains retained in checkpoint
305. Its startup cause did not identify which subcondition failed. These tests
prove a real failure mode consistent with that result, without claiming the
unrecorded original interleaving has been uniquely reconstructed. New failure-only
short-recording diagnostics retain dispatch timing, disposition, render cause,
published startup frames/time and output count. No logging was added to RT code.

## Tests and evidence corrections

Five new PipeWire cases cover finite/live readiness after a stale caller sample,
subsequent exact PCM delivery, bounded future readiness, malformed finite EOF and
cancellation. Four WASAPI cases cover exact finite completion with the original
receipt, delayed future-readiness timeout, malformed EOF and cancellation. Existing
RT allocation/free/blocking-lock counters remain active. Shared PipeWire processing
now explicitly checks silence before a future readiness time and unchanged audio
when the next independent clock reaches it.

The original two-test failure before the production fix is retained. An initial
new WASAPI timeout fixture failed because it bypassed the render callback for the
final lifecycle observation and expected diagnostic readiness to disappear. It
now follows the actual callback/lifecycle sequence and distinguishes a retained
diagnostic receipt from accepted readiness (zero acquisition timestamp/no PCM).
The first seven-suite qualification then found the older composed PipeWire test
still demanding immediate failure for future readiness. That case now requires
zero consumed frames and silence while deferred, then exact PCM at a fresh clock.
Product readiness validation stayed unchanged during these fixture corrections.
All failed evidence and subsequent qualification remain retained.

Final clean-source qualification passes all seven selected suites in both
configurations: Release 27.479 seconds and Debug 32.333 seconds. These include
native monitor startup, the original one-frame owned recording, planar rendering,
render binding, composed processing, bounded startup and durable route outcomes.
Selected targets had no pending build work, and source/executable hashes remained
stable. The full logs retain exact checks and RT audit results. These tests execute
platform-independent consumers and simulated native owners on Linux; native Windows
runtime and physical audio remain separate gates.

[evidence index](../tests/results/M2/2026-10-15-startup-snapshot/index.json)
contains the original failing tests, corrected qualification, build logs, source
and executable hashes, scheduler provenance and audit scripts. All six affected C++ translation units pass MinGW syntax checks without
diagnostics. This remains separate from native execution. The verified archive
contains 54 files in 53 unique payloads, 338,834 bytes, SHA-256
`e9a4f65b9d5347da94275a45f99c04da55a3fb72c79791384d43bad7ed463f0d`.

## CI scheduling

The preceding run passed all 199 Linux CTest cases, but its second job reached the
unchanged 30-minute budget during later archive verification. Actual test time was
369.97 seconds on one shard and 981.94 seconds on the other. Scheduling weights now
come from both complete original logs, with source revision and log hashes. The
new protected-history bank has no remote measurement yet; its disclosed estimate
is twice its qualified local Debug duration (556.595 seconds). Estimates control
placement only and never alter test deadlines or assertions.

The complete discovered CTest inventory is now partitioned across three Linux
jobs. The current 200-test inventory models approximately 636.22 seconds per
shard (59/70/71 tests); remote execution must verify this estimate. Every job still
runs the existing archive checks and keeps its 30-minute limit, and the unchanged
required aggregate demands that every shard pass. Coverage/uniqueness, stable
partition order and unknown-test handling pass the scheduler test. No tests or
archive gates were removed. This increases remote CI job count from six to seven;
local testing remains one low-priority worker.

No new dependency, license, persisted format or production UI text is introduced.
No installed app, physical audio route, VM or equalizer repository was changed.
Native Windows execution of this source and repeated protected desktop workflows
remain next work; these corrections do not complete M2, M3–M11, F/Q/C/N, X004/X005
or all-Europe localization.
