# Prepared monitor output transport

`PreparedMonitorOutputBank` adds an original GPL-3.0-only C++20 handoff from one
processing producer to one rendering consumer. It reserves/touches complete
sample storage and bounded ready/free SPSC queues before either owner starts.
Retirement requires both owners to join. Native duplex remains disabled.

## Contract

`MonitorOutputOrdinal` is an integer monitor stream position, deliberately
separate from capture frames, QPC, project frames and native render ticks.
Whole blocks carry a fixed run generation, contiguous original output ordinal
and frame count. Configuration validates 1–256 channels, 8–384 kHz, 1–65,536
frames per block/pull and 1–256 slots, plus whole payload/ledger credit. The rate
is format metadata; this transport performs no clock conversion or resampling.

Publication checks the entire finite float32 block before copying. Signed zero,
channel order and finite float headroom survive unchanged. Nonfinite PCM,
mismatching generation/ordinal/extent and counter overflow produce an immutable
producer fault. No partial block is accepted. Full storage returns Backpressure
with no cursor/counter/fault change; the processing owner must retain the exact
block until acceptance or explicit failure. It must not pull more DSP output and
overwrite that pending block. Unlike a native capture loss, this is retryable
processing pressure, with no waiting inside the prepared method.

Render pulls can cross block boundaries and retain a partially consumed block.
Each pull is bounded by the entry queue snapshot and maximum admitted pull size;
it cannot chase concurrent publications indefinitely. It copies only the available
prefix, leaves the destination suffix unchanged, and reports the exact integer
first ordinal/count. Invalid destinations, Empty, Drained and cancelled/refused
reads leave all destination data untouched. The transport never invents silence
or stale PCM. Empty alone is not an underflow: native activation, scheduling and
deadlines must determine actual loss and call `signalUnderflow` explicitly.

The render owner retains its own immutable failure receipt with actual next
ordinal and requested frames. Producer and consumer fault cells each have one
writer, are independently published with release/acquire, and both survive
simultaneous failure. No global wall-time first-fault ordering is asserted.
Producer faults leave an accepted prefix available for diagnostic draining,
with Fault phase on every receipt. Native rendering must act on that phase;
it cannot treat a diagnostic prefix as continuing healthy playback. Consumer
failure prevents further pulls and publication. Fault takes phase precedence.

Stop prevents new publication and drains the accepted prefix. Cancel prevents
further pulls, including already queued PCM after producer completion; an
in-flight operation may complete. After both owners join, `settleAfterJoin`
faults an abandoned producer without inventing a packet and ends it. Cancel or
consumer loss then discards only the remaining monitor backlog, retaining exact
accepted/delivered/discarded counts and both faults. Settlement is idempotent.
An ordinary finished or producer-faulted prefix remains drainable. No raw
recording route is owned or cancelled by this object.

All prepared operations perform bounded arithmetic, sample validation/copying,
atomics and queue operations, without SDK calls, allocation/free, blocking locks,
logging or disk I/O. See [ADR120](decisions/120-monitor-output-transport.md).

## Frozen evidence

Source `65924c6774f8d0b3e8849c28f6395654c1805eff` passed Linux Release0.04s,
ASan/UBSan0.69s with leak/UB checks, and TSan0.59s with halt-on-error.
Each execution made788 serial/extent/audit checks plus two actual concurrent
producer/render comparisons:

- Three channels,20,000 variable blocks,319,935 frames and959,805 float-bit comparisons.
- 256 channels,1,024 variable blocks,16,369 frames and4,190,464 float-bit comparisons.

Synthetic producer retries/yields are outside prepared methods. Queue wrap,
scratch poisoning, partial pulls, headroom/signed zero, independent consumer
scheduling and original ordinals above2^63 are covered. Separate serial cases
check stable full-queue retry, invalid/oversized/empty destinations, nonfinite
whole-block refusal, generation/ordinal/extent faults, last UINT64 ordinal,
Stop prefix, cancel after partial consumption/completion, explicit underflow,
late dual-owner failure, abandoned producer, exact joined discard and credit
admission/retirement. Guarded C++ and Linux-interposed libc allocation/free/
pthread-lock counts were zero; this does not cover every syscall or deadline.

The [55,006-byte packet](../tests/results/M2/2026-10-11-monitor-output-bank/summary.json)
retains10 members,559 selected source inputs, three executable hashes and full
verbose build/CTest/LastTest logs for each profile. Git blob and working-tree
hashes distinguish the explicitly verified CRLF checkout of the Windows
preparation script. Standalone gzip/member and frozen source checks passed.
No executable/media is included. Actual MSVC execution is still required.

## Next workflow

Compose original raw writer/stamped metadata, capture fanout, causal monitor
buffer/controller and this output bank under matching off-RT admission.
Processing must retain pending output on pressure, validate actual control-point
freshness and establish clocks before activating monitoring. Native render must
respect phase/count receipts, handle genuine underflow without replay, and join
all owners before retirement. Persist original raw-to-project correspondence
separately; monitor ordinals are not recording alignment. Native SDK, installed,
physical/load and processing-quality gates remain open, as do earlier DSP quality
failures, M2–M11, X004/X005, F/Q/C/N and European language qualification.
