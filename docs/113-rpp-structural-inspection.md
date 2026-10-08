# X004: first bounded RPP structural inspection

Date: 2026-10-08. Native-project compatibility: **unqualified**.

`sc-reaper-structure` inspects an in-memory text container on a non-audio worker.
It retains original bytes and a line/block inventory with parent and byte ranges.
Unknown fields, plugin-shaped state, embedded script-shaped text and external
path-shaped tokens remain opaque. There is no plugin/script execution, media
lookup, decompression, track creation or project write.

The accepted structural subset has one `REAPER_PROJECT` root, unquoted ASCII block
tags, whitespace/newline-delimited lines and standalone closing delimiters. BOM,
CRLF, blank lines and a final line without newline are retained exactly. Refuse
binary NUL, quoted delimiters, invalid tags, multiple roots, data outside the root,
unbalanced/truncated blocks and out-of-range source views. Foreign semantic
properties remain **unverified** even when their tag is familiar.

## Resource and ownership contract

Default trusted limits: 16 MiB input, 200,000 lines, 65,536 content bytes per line
(including CR, excluding LF), 128 nested blocks. Bigger limits require deliberate
worker configuration and shared memory budget. Scan each line within the line
limit and check cancellation before entry and at each preflight/parse line.

Admit owned source bytes, structure rows and transient parent stack before
allocating those banks. Retire scratch before shrinking the lease. Returned
documents are unique and immutable; moving construction transfers ownership.
Move assignment is intentionally unavailable so old storage cannot outlive its
returned credit. Borrowed views cannot outlive the document. Input views must
remain stable through the call; the inventory is built from its retained copy.
Input storage owned by the caller needs separate accounting. The ledger tracks
payload, not allocator overhead, process RSS or hard OS memory limits.

This library alone is not process isolation or a user-facing file importer. No
native REAPER writer/version envelope or property conversion is qualified.
The synthetic test input is original project code, not vendor content or a native
project corpus. See [ADR081](decisions/081-bounded-foreign-project-outline.md).

## Evidence

`sc-reaper-structure-tests` checks full byte inventory, parents/block extents,
opaque unknown data, move/credit lifetime, exact line boundaries, malformed input,
input/line/nesting/shared-budget refusals, scratch retirement and cancellation
after actual admission. Linux Release and ASan/UBSan pass; Windows MinGW builds.
Hosted MSVC execution is queued with the required native check; it is not yet
claimed. Evidence receipt: [local synthetic inputs and exits](../tests/results/X004/2026-10-08-rpp-structure.json).
No VM or audio endpoint is needed for this step.

## Next implementation task

Add a bounded isolated file-loader/inspection worker and machine-readable report
with source hash, format/version uncertainty, structural ranges and explicit
unverified properties. Cancellation must retire the worker without touching the
original or destination project. Then establish rights-cleared projects generated
by a pinned REAPER version and map a first audio-track/clip subset into import
intermediate state with preserved unknown data and an acceptance/loss preview.
Do not feed foreign tags directly into the current audio session schema.

Bitwig `.bwproject`, Cubase `.cpr`, other native suites and exchange adapters
remain in the [X004 register](10-project-import.md); none is completed by this
outline. Processing quality, native compatibility, functional DAW parity and
bundled content remain separate gates.
