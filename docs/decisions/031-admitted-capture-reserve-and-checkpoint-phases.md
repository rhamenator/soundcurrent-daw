# ADR-031: admit capture reserve independently of disk block size

Date: 2026-10-06. Status: accepted for bounded storage-burst qualification.

## Context

M2d4c1 measured a 4.272675-second writer gap while all fixed 2.730667-second pools
filled. Wall time established checkpoint backlog in that run without isolating
syscall service from scheduling or proving original historical causes. Enlarging
write blocks would change batching/durability granularity; lowering checkpoint
frequency would extend the intended recovery interval.

## Decision

- Preserve write blocks, raw/native timestamps, callback bounds and regular
  checkpoint frame spacing. Admit capture pool slots separately, capped at 256
  with fixed 512-token SPSC queues. Playback keeps 32 slots.
- Offer explicit 2/5/10-second desktop reserves, default 10; capture the choice at
  Prepare. Normalize by rounding whole slots off audio; refuse impossible requests.
- Include fixed pool/object memory in per-pipe and shared aggregate admission;
  preserve the 256 MiB aggregate ceiling and preflight before recording jobs.
- Distribute unspecified first checkpoint thresholds across one regular interval;
  preserve explicit phases and all subsequent intervals. A shorter first checkpoint
  cannot increase nominal regular spacing. Block quantization limits separation.
- Verify four-second stall absorption and twelve-second exhaustion with full
  independent raw/output/hash/journal oracles. Keep the unchanged long native
  duration/deadline gate and every inherited unexplained observation.

## Consequences

More memory is touched during preparation and metadata queues are larger even for
legacy small pools. High-rate or high-channel requests may fail and require a
smaller reserve. Live monitoring is not buffered by this reserve. A blocked disk
still delays durable wall time and may exceed any finite reserve; uncommitted
memory is not crash-safe media. Sustained throughput, original causes, physical/
Windows behavior and power-loss reliability require separate evidence. No project
schema, library/license, dependency, proprietary algorithm or equalizer source
change is introduced. Copied equalizer inputs are re-audited before the checkpoint.
