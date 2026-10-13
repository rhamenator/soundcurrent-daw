# ADR 146: Copy the verified finite checkpoint without changing the original

Status: accepted, 2026-10-13. Qualification: [Checkpoint 247](../247-interrupted-prefix-copy.md).

## Context

An interrupted recording may have a durable common prefix, additional uncommitted
samples, and no finalized media or native outcome. Complete original WAV export
requires a wholly finite finalized container; recovery needs a separate operation.
PipeWire's paired checkpoint may also lag its standalone raw journal.

## Decision

Add a framework-independent, off-RT prefix-copy primitive shared by both native
routes. Verify the original common checkpoint, retain the exclusive inactive raw
lease through publication, pin the selected source, and copy exactly the positive
finite prefix. Hash all committed sample bytes against the verified original;
check the entire pinned source before/after copying. Write a new RF64 float32
container with unchanged sample bits, native rate and channel ordinals.

Keep complete byte-for-byte original export unchanged. Copy receipts retain the
original raw checkpoint and native status. Recovery does not finalize, resume,
normalize, align, EQ or attach the original. Reject empty playable prefixes,
active/missing leases, changed or mismatched sources, exceeded byte/memory bounds,
existing/raced destinations and pre-publication cancellation. Flush and publish
without overwrite; a post-publication directory-flush warning retains the valid
published-file receipt.

PipeWire retains state, receipt and raw leases in the established order. Its
manifest-bound raw journal selects the common prefix even when the standalone
journal is newer. Existing inspection is factored into internal held-lease paths;
validation and versioned outcome meaning are preserved.

## Consequences

The header-only WAVE extent API explicitly distinguishes exact containers from
checkpoint containers with excluded trailing bytes. It neither verifies audio
nor supplies a content digest; full WAVE admission continues using exact scope.
The copy primitive performs separate prefix/hash/finiteness checks before publishing.

There are no new libraries or source-journal migrations. Existing libsndfile,
owned-file admission, SHA-256 and no-overwrite publication infrastructure remain.
Raw/native copy defaults to16 MiB workspace. PipeWire uses64 MiB, matching its
existing export/admission scopes because state verification alone admits24 MiB.
Caller quotas remain enforceable. Source and output size are bounded separately
by the declared byte policy; work uses a fixed64 KiB sample page, not a buffer
proportional to recording length. OS I/O remains cooperatively cancellable,
without a hard syscall deadline or total process-memory guarantee.

Desktop review/adoption, Linux/Windows UI qualification, new-source Windows runtime
and physical audio remain required follow-up work. These APIs alone do not establish
completion of the user recovery workflow or the full suite.
