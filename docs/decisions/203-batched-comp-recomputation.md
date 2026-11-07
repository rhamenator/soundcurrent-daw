# ADR 203: bracket paired output recomputation as one batch

Status: accepted for implementation; native Windows qualification pending.

## Evidence

The retained installed Windows render remains slow. A Linux verification profile
of the same history family attributes about 59% of sampled CPU time to the crypto
library. A separate diagnostic counts 729,204,805 bytes passed to SHA-256 update
calls while verifying a 12,204,541-byte, 225-file project. These are verification
workloads, not complete Windows render benchmarks.

`verifyFiles` previously streamed each output separately. Each streamer fully
verified both source candidates and original nominal renders before and after
its own output. Two outputs therefore repeated the same paired admission four
times within one recomputation.

## Decision

Add a bounded paired streaming operation. It admits every selected output's extent
before any callback, verifies both candidates and original nominal renders, streams
selected sides sequentially, then performs the full paired verification again.
It returns reports only after that final check succeeds. No verification result
is retained across calls. The existing one-fragment API delegates to the batch
with exactly one selected side and preserves its before/after checks.

Use this batch in complete assembly-file recomputation. Both output files remain
pinned from their independent WAV validation through recomputation. One decoded
output cache is used at a time; a ledger-charged slab covers the maximum selected
channel count. Both reports retain charged payloads. Independent bitwise sample
comparison, complete output hashes and the outside-edit media review remain.

The before/after original-history traversal, publication protocol, helper identity,
project state and adoption guards are unchanged. One cooperative deadline covers
the batch; its value is not increased. Exceptions, cancellation and mutation can
leave tentative callback bytes, but produce no successful batch reports and no
new authorization to publish or adopt them. Callers must keep sinks unpublished.

## Alternatives and limits

Do not introduce a persistent digest cache or skip original-history checks on the
strength of a previous run. Do not remove sibling verification. Those approaches
would require a different mutation/identity contract and separate evidence.

Publication still uses its existing per-fragment streams; this change targets
recomputation, including historical assembly verification. It does not claim that
all redundant work has been removed. The batch has at most two pinned output
handles and two report leases; its resource cost is explicit and bounded.

## Acceptance

Require bit-identical batch and single-fragment results for the existing physical,
resampled and protected fixtures; correct one-sided behavior; no samples before
preflight admission; cancellation before sibling output; rejection of late marker
corruption; and full resource-credit return after success and exceptions. Retain
the existing publication, history, nominal-proof and altered-output tests.

Linux measurements and acceptance are recorded in
[checkpoint 335](../335-batched-comp-verification.md). Windows native and installed
responsiveness remain separate gates.
