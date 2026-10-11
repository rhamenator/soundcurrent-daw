# ADR137: version original native startup evidence and migrate explicitly

Status: accepted for auxiliary state; native factory remains disabled.
Date:2026-10-11.

## Decision

Declare native auxiliary minor1 before creating a new pending transaction.
Retain explicit legacy0 writing and unchanged raw/project schemas. Preserve typed
original readiness, source dispositions, native warmup clocks/leases/results and
terminal phase with exact keys and stable tokens; retain existing parser bounds.
A Complete claim must match the declared tuple, published full capacity and
successful original SDK evidence. Invalid native diagnostics remain diagnostic.

Expose explicit, idempotent1.0-to1.1 migration under the same exclusive inactive
job lease and verified raw-prefix/raw-journal binding. Copy legacy observations
without inventing startup facts; never rewrite raw media, resume capture or
infer successful monitoring from saved PCM. Keep before/after migration publication
boundaries explicit. Unsupported versions and active/mismatched/corrupt jobs fail.

## Evidence and limits

[Checkpoint207](../207-native-state11.md) binds composed real prepared processing
with simulated SDK outcomes to durable raw hashes, new state round trips, malformed
Complete rejection, terminal/partial recovery and actual process interruption
boundaries. Linux sanitizer profiles and Windows compilation are independently
recorded. Native Windows execution, installed/physical audio and timing remain
required. Mono-input/stereo-output mapping remains a concrete factory dependency;
matching-channel composition does not establish that route.
