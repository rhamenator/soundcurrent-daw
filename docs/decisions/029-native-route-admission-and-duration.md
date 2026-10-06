# ADR-029: admit DSP after all native links are active

Date: 2026-10-06. Status: accepted for the scoped native owner/fixture checkpoint.

## Context

Proxy creation was asynchronous; activation could begin capture while selected
channels were still negotiating. An initial short fault case exposed one raw
plane delayed a quantum from its first sample. Its precise cause is not proven.
The old finite timing helper retained only8192 samples and lacked99.9%/period
ratios, so it could not establish full 30-minute timing coverage.

## Decision

- Observe and own every selected link's listener on the existing PipeWire client.
  Native activation precedes buffer negotiation; gate engine/capture admission
  until every link is Active, with a bounded control-side wait and rollback.
- Until admission, publish bounded certified silence without advancing DSP,
  capture or origin. Later route inactivity/error closes admission and retains
  DeviceLost. Never resynchronize/trim/relabel raw data to hide a mismatch.
- Retire listener/proxy state under the control loop after native shutdown.
  Add no callback allocation, blocking, disk, GUI or logging work.
- Add optional RT-safe current-clock observation for opt-in instrumentation,
  including gated/shutdown callbacks. Keep timing acquisition in fixtures.
- Admit fixed full-duration storage, retain every elapsed/period pair, and deny
  qualification on overflow/missing periods/clock failure. Sort off RT; use
  nearest-rank99.9% and maximum period ratios. Qualify the fixed native workload
  independently of sample correctness, physical audio and Windows.

## Consequences

Startup can take up to the bounded three-second route wait; desktop/native owners
already invoke activation on workers. The engine first origin now belongs to a
fully admitted callback. PipeWire remains unchanged. No dependency/license change.
Short tests and full ordinary suites qualify this checkpoint, while the original
delayed plane, all inherited reliability observations and the actual 30-minute/
physical/platform/release/full parity gates remain open until evidenced.

## Duration outcome

The first 30-minute native attempt failed after about 55 seconds with capture-pool
exhaustion. The initiating lane and every unequal valid prefix are retained;
original worker checkpoint timings are absent, so the backlog cause is unproven.
A fixture-only120-second instrumented run passes sample/hash/Save-reopen checks
but exceeds the maximum callback threshold (83.6002% of period). Its observed
worker intervals do not establish the original failure cause. No memory budget,
slab size, checkpoint durability, tolerance or timing gate is changed. A read-only
offline verifier checks all retained raw prefixes and the common output range
without saving/trimming originals. Next diagnose worker phases/queue occupancy
and callback tails before declaring or repeating30-minute qualification.
