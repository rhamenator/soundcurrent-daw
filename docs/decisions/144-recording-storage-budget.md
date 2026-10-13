# ADR 144: Prepare recording storage from explicit stall and callback bounds

Status: accepted for the owned recording backend; desktop presentation pending.

Measured disk pauses exceed small fixture raw buffers. Allocate bounded runtime
SPSC arrays before activation, admit raw/receipt capacity from an explicit duration
and minimum callback size, and enforce that minimum at native boundaries. Keep
existing larger receipt capacity. Charge rounded metadata arrays and raw PCM through
the aggregate payload ledger; reject unsupported requests before native preparation.

The first owner integration uses two safety packets. No budget means legacy slots
and no duration claim. Do not infer minimum cycle size from maximum quantum,
raise global queue bounds without admission, or make queues unbounded. Do not alter
paired-prefix durability, failure reporting, or callback allocation/lock rules.

Persist the new declarations in owner schema 1.2, preserve reading 1.0/1.1 and refuse
unknown versions. Outcome encoding remains 1.1. A larger prepared buffer provides
bounded storage headroom, not an unlimited or sustained-throughput guarantee.

Acceptance: within-budget stall preserves exact raw audio and finite output;
beyond-budget raw pressure reports failure and retains the accepted verified prefix;
minimum quantum violation publishes no raw packet; quotas and effective declaration
survive reopening. See [Checkpoint 242](../242-recording-storage-budget.md).
