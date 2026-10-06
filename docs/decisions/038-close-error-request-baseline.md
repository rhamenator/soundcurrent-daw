# ADR-038: Controller error baseline for desktop close requests

Date: 2026-10-06. Status: accepted; scoped Linux UI qualification.

A newly displayed error is not necessarily an error from the active close request.
The controller may have published it before Close while the GUI had not polled it.
A deterministic fixture proves the old behavior cancels a valid clean close.

Record the published serial when accepting Close. Display every observed error,
but cancel an active request only for a serial newer than its baseline. Snapshot
the baseline before focused-widget commit; a new error from that commit must
still cancel. A retry starts a new baseline. Save failures preserve dirty and
saved state and require an explicit new request after repair.

Keep this state in the GUI; do not change controller error semantics, project
serialization, real-time code or recording shutdown ownership. Linux clean,
dirty and new-failure/retry workflows pass in Debug and Release, with the relevant
desktop groups also sanitized. Windows runtime and the full product remain open.
Earlier unidentified close failures remain retained independently of this proof.
