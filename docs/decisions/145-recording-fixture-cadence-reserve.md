# ADR 145: Account for service work and declare manual fixture reserve

Status: accepted, 2026-10-13. Qualification: [Checkpoint 246](../246-recording-cadence-reserve.md).

## Context

The simulated native device added a full ten-millisecond wait after actual work
while advancing its audio clock by ten milliseconds. A retained Windows startup
failure reached only 1.89 nominal seconds during a five-second observation.
Separately, a manual concurrency fixture used 8,192 frames of capture capacity,
although the product already offers an explicitly admitted 2–20 second reserve.

## Decision

Provide a bounded off-RT remaining-period wait using the existing prepared timer.
Use it in the fake SDK with work/wait/period observations. Start each service
period at the actual current time; do not replay missed cycles. Keep production
fixed waits, confidence requirements, audio coordinates and observation deadlines.

Select the existing two-second reserve explicitly for manual concurrency, retain
its callback geometry, and test held-disk behavior both within and beyond admitted
capacity. Preserve the original small-pool pressure tests and prior failure logs.

## Consequences

No dependencies, project schema, UI controls or product defaults change. Fixture
work or scheduling can still overrun a period. Declared reserve consumes admitted
memory and protects only its bounded accepted prefix; it cannot guarantee timely
filesystem service. A green later run is evidence for that source/run, not proof
that all earlier platform timing failures have been explained or eliminated.
