# ADR 018: passive discovery, cooperative activity leases and separate I/O owner

Date: 2026-10-06. Status: accepted for S8c.

## Decision

Discover bounded checkpoint metadata in a Qt-free API. Verify audio only after
explicit Review, then retain the existing preview/consent/revalidate/copy/verified
admission workflow. New writers hold per-job OS lifetime leases; missing markers
are legacy/unconfirmed, never proof of inactivity. Metadata chains reduce repeat
recovery offers while preserving explicit access to originals.

Run automatic Open/asset-change discovery on a separate latest-slot QThread,
with root/epoch/serial receipts and cooperative cancellation. Keep audio
preparation and graph/callback work independent. Close dismisses recovery dialogs
and waits for this fifth owner’s closed receipt together with project/playback/recording/export owners; controller destruction joins the threads.

## Consequences

No whole-project audio hashing during passive scans and no automatic copy/delete.
Large inventories disclose truncation; external changes need Refresh. Legacy
activity remains uncertain. Canceled copying can leave an owned recoverable job.
Filesystem calls must return before cancellation; cooperative leases do not
provide hostile-filesystem containment. No new library or project/journal schema
version is introduced. [Contract and acceptance](../28-recording-discovery.md).
