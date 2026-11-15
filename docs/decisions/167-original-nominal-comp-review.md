# ADR 167: verify original nominal jobs before comp assembly

Status: accepted original receipt review; binary attestation and final adoption open.

Treat canonical asset identity/path and the preserved raw stretch anchor as the
expected original operation, rather than accepting a current WAV with matching
metadata alone. Require a retired original job lease. Reuse the existing owned
stretch verifier under independently supplied policies, then compare its complete
asset and anchor with the canonical nominal pair. Repeat review after streaming.

Use the separately retained nominal asset when an existing processed handle window
is present. Keep unsupported imported/legacy derivatives explicit; regeneration
from retained raw audio remains the recovery direction, without inventing receipts.
Do not copy policy grants from an unverified marker to satisfy verification.

The old stretch protocol has no executable digest. Keep that limitation explicit
and version any future executable attestation. Actual-process acceptance records
binary/source identities separately. Receipt, arithmetic and scope checks do not
replace processing-quality evidence or final canonical paired adoption guards.
