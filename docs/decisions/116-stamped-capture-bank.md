# ADR116: original-packet capture bank between native owners

Status: implemented core, local ownership gates underway; no native desktop
duplex integration. Follows [ADR114](114-windows-duplex-clock-boundary.md) and
[ADR115](115-bounded-clock-correlation.md).

## Ownership and admission

Prepare one bank for each capture stream, with immutable endpoint/stream/generation,
sample rate, original channel order/count, maximum packet extent and slot count.
Reserve object/queue/sample payload credit and allocate/touch the entire float
pool off RT. Existing ResourceLedger and SpscQueue infrastructure is reused within
this repository; no equalizer repository or third-party dependency changes.

The capture owner copies a complete packet into a free slot before publishing its
token. The processing/render owner acquires one view and returns that exact token
once. Native SDK leases remain on their acquisition/release owner; reusable copied
scratch cannot escape its callback. A bank copy keeps the published packet valid
after both the SDK and intermediate scratch have been reused. No bank view survives
release, reset or destruction. Reset/resize are deliberately absent.

Two512-entry SPSC queues carry free slot IDs and ready packet tokens. At most256
admitted slots exist, so a valid token cannot fill the ready/free queues. Each
queue retains one writer/reader after initialization ownership transfers. Queue
counters may wrap; packet sequence UINT64_MAX is a reserved sentinel and sequence
overflow is terminal to prevent reuse of stale lease identities. Configuration
offsets permit wrap/overflow acceptance without active reset.

## Samples, timestamps and failure policy

Preserve original packet identity, frame position, 100ns QPC, frame count and flags.
Copy original interleaved float bits and channel order, including headroom and
nonfinite values; downstream raw/DSP policy remains explicit. A flagged silent
packet gets zero samples with its original flag and never reads SDK backing.
No packet trimming, partial publication, downmix, timestamp synthesis, inferred
end timestamp or project alignment is performed. Original metadata can feed the
separate clock core, but the bank itself performs no drift correction.

Reject invalid identity/extent/backing/flags/timestamp, position overflow, later
discontinuity, position gap/overlap, backwards/equal QPC or sequence overflow.
Startup discontinuity is permitted with original metadata. Queue overrun is a
sticky terminal error, with the first rejected original stamp and rejected-frame
count preserved. Returning storage later cannot silently recover from lost audio.
An impossible queue invariant failure is terminal and its orphan storage is
retired only after owners join. Nonfinite processing policy remains downstream;
no original float bits are silently replaced by this bank.

Consumer Empty is explicit temporary underflow, not silent audio or a fault by
itself. Warmup/callback partitioning may naturally produce it; the later causal
monitor owner must enforce its declared delay/underflow policy. Consumer failure
is an explicit atomic signal to the producer. Acquiring a second lease or releasing
a stale/altered/foreign view is refused without freeing somebody else's slot.

Stop/cancel requests do not wait for owners; an in-flight publication may finish.
The capture owner (or control after capture-owner join) finishes production.
Cancellation before finish dominates Stop. Accepted prefixes remain drainable
after Stop/cancel/fault. The final publication precedes release-published Done;
consumer empty/Done handling retries the queue once to cover that race, never in
an unbounded loop. Join both owners before bank destruction and ledger retirement.

## Qualification and remaining work

Tests must exercise original bits/metadata, full current channel width, SDK/scratch
reuse, FIFO/queue wrap, lease misuse, strict overrun/error prefixes, Stop/cancel,
admission/retirement and two concurrent owners under RT and sanitizer checks.
Linux and Windows execution have separate scopes; test-only interception does
not prove every library/syscall path or callback deadline. Windows tests verify
flag values against SDK constants at compile time without calling audio endpoints.

This is a reusable handoff primitive, not a native Windows recording workflow.
Next: causal monitor delay and bounded drift-resampling control, original raw
writers with durable versioned clock/project mappings, SDK owner composition and
Windows desktop duplex preparation. Monitoring correction cannot serve as raw
alignment evidence. Full P005/M2, installation, hardware/load, fault recovery,
localization and the remaining F/Q/C/N gates stay open.
