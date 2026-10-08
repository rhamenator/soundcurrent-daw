# ADR 078: Native desktop success requires independent retained media

Date: 2026-10-08. Status: accepted for one-track preview qualification.

An actual native desktop workflow can return success while its captured audio
differs from the engine expectation. The original non-silent Windows run returned
0 but showed a 480-frame startup attenuation in an independent sample comparison.

Require original media, finalized journals/native timing origins, actual applied
edit/Undo receipts and a source-independent float sample oracle alongside the
native GUI result. Keep raw input, live output and saved-state export comparisons
separate. Reject altered/rehashed media and success flags. Preserve failed runs.

Use an explicit owned device and a separately threaded bounded test generator;
do not change endpoint/default settings or introduce SDK control work into product
callbacks. Hardware fixtures are opt-in. Hosted tests replay retained evidence
read-only and must say they do not replay native audio.

The accepted silent-lead workflow leaves the original startup discrepancy open;
it does not establish full-range non-silent startup, physical microphone routing,
latency, sustained scheduling, Windows duplex or installer parity. A developer SDK
runtime does not establish compiler-free deployment.
