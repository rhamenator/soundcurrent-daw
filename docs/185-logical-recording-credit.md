# Synthetic recording credit under stalled CI disk service

Native MSVC job114406484458/run38117923095 passed the manual control workflow
in19.67s and the other51 workflows, then failed recording-recovery. Its paced
logical source filled32×1,024 frames during a3.3102091s observed write gap. The
maximum audio-flush interval was2.4620152s and the following journal interval
0.8481913s. These include scheduling; they do not isolate filesystem service time.
Capture correctly returned QueueFull, accepting89 and rejecting38 frames of the
packet at417,703. Finalized417,792 frames therefore could not satisfy480,000.
The elapsed synthetic production was41.09s. This is direct retained diagnostic
evidence of this failure, not a diagnosis of earlier manual-control timeouts.

## Fixture correction

Frozen source `cf0b9ed` changes only the synthetic recording test. The logical
producer waits outside marked callbacks until accepted-minus-published-written
frames plus the next packet fits a30-slab credit window. Two slabs of slack cover
acquired/unfinished ownership. The worker publishes written frames after draining,
releasing and checkpointing, so this is conservative returned credit. A5s bound
reports a credit failure rather than retrying a terminal/partially rejected packet.

The first test-owned WriteHashBegin is deliberately held until this credit window
is reached. The logical producer releases it while waiting; assertions require
the gate, the actual credit wait, no gate timeout and no instrumentation on audio.
Wall-clock sample-rate sleeps are removed from this functional fixture. Logical
time advances only after whole-packet acceptance. This is not a native device
backpressure algorithm or evidence of real-time throughput on the CI disk.

All480,000-frame, sample/header, raw-overs, finalized journal, attachment/save/
reopen and callback allocation/lock assertions remain. Production pool sizes,
capture QueueFull, writer/checkpoint behavior and native source timing are unchanged.
Separate forced overflow/recovery tests remain in the same suite. CTest60s and
all5s fixture bounds remain unchanged. A passing run cannot establish tolerance
of unbounded disk stalls.

## Evidence and next gate

Linux Debug recording-recovery passed0.19s with292 observed scheduling-dependent
checks. Its seven observed credit waits included the forced first-write gate;
all480,000 frames and recovery assertions passed, audited callback operations zero.
The source/log-only packet retains the full failed Windows job/status, local
build/CTest and seven selected input hashes plus executable identity. No executable
is included. Standalone decompression/member and selected byte checks passed;
inputs are not a complete compiler/system/dependency closure.

Fresh current-MAIN Windows execution and all required checks are needed before
PR119 admission. Earlier unknown manual wait cause remains distinct; the latest
manual workflow passed, and caller/early-owner diagnostics remain in place.
Native/installed/physical/load/quality and the full DAW goal remain open.
