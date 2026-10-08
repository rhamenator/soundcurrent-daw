# Playback output preparation rollback

PR #53 merged the admitted capture copy/release change and hosted native tests
at `13a7d6845e3511b8d226d22c7dacffba713071db`. Its full Windows endpoint
capture and installed qualification remain open. While its independent VM disk
verification continues, inspection found the same partial-admission issue in
`WasapiPlayback::connectOutputs`.

## Product change

The previous owner assigned the prepared interleaving bank to state before
constructing the native render stream. If SDK preparation threw, the unused bank
held project-wide memory credit until replacement or destruction. Now both owners
remain local until preparation succeeds. A failure retires the bank and leaves
no stream/native channel extent; success commits both while still inactive.
No callback runs before explicit activation. Replacing an earlier inactive route
joins/retires that route before preparing its replacement. Fresh inventory checks
and explicit selection precede preparation; no default output is selected.

No rendering algorithm, sample oracle, queue timing or Stop semantics changes.
No new dependency or equalizer modification is involved.

## Regression evidence

The test compiles the actual production `src/wasapi_playback.cpp`, graph, reader,
project store, ports and interleaver. Only SDK-facing inventory and render symbols
are linked to test doubles. The double verifies that output credit was already
reserved when native construction begins, then injects an I/O preparation refusal.
This exercises the real exception/ownership path without activating an endpoint.
The double is isolated in its test executable and cannot enter a product target.

The unmodified owner from `13a7d68` fails with actual exit 1 and
`Failed native preparation retained output memory credit`. The fixed owner passes:
three failed preparations restore active bytes/owner counts; activation after
failure is refused; an inactive retry owns exactly one interleaving bank; failure
while replacing that route retires both old and tentative banks; another retry
succeeds; Stop/destruction returns all graph/reader/output credit. No position
advances, activation occurs or default endpoint is queried. Peak credit is allowed
to record real tentative allocations rather than being mislabeled a leak.

[Retained red/green and sanitizer results](../tests/results/X007/2026-10-08-output-owner-rollback.json)
include actual exits, source/test/binary hashes and scope. Linux Release and
ASan/UBSan with leak detection pass. MinGW cross-compiles the production library
and regression executable; that is build evidence, not Windows execution.
Native MSVC runs the same SDK-injected owner
case as part of its required job; execution of this addition is pending. Actual
SDK preparation/failure, audio, GUI and installer acceptance are separate gates.
This is not a Windows endpoint test or full parity claim.

## Native experiment preparation

The VM comparison now uses two new source-verified build directories rather than
rebuilding over copied objects: baseline `097bbf9` (346 source inputs) and capture
candidate `d4c8747` (349). Both use the same copied SDK, discovered MSVC generator
and media DLL/import-library hashes. A fresh directory is required; inputs are
verified before and after compilation and compiler/binary identities retained.
This avoids treating stale objects or ambiguous ZIP timestamps as new code.
Prepared limited-owner runners retain actual sessions, process identities,
exits, refusals and partial takes. Their PowerShell syntax parses locally;
no script or native endpoint has executed on the new clone yet.

The same rate-limited disk verifier is temporarily suspended after the owner
reported continuous disk activity. Disk reads dropped almost to zero after
suspension; 227 GB disk space and 52 GB available RAM remained, with no disk swap
use. This does not identify the earlier crash. Original VMs, template and
ongoing equalizer work are preserved. Next: finish disk equality verification,
boot the independent capped clone, execute frozen capture admission and v1/v2
comparisons, then the separate production-EQ Stop investigation. Existing local
preview binaries remain unchanged; the full frozen-reference goal remains active.
