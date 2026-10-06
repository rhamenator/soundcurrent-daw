# ADR-045: Retain per-lane punch prefixes across native faults

Date: 2026-10-06. Status: accepted bounded qualification; full product gates open.

Use a separate opt-in fixture with the production native duplex owner and disk
workers. Remove only owned routes, inject disk exceptions only at existing
worker-side boundaries, and wait for native/control/disk joins before inspection.
Retain the first terminal result and original per-lane exception; cancellation
withholds every original receipt. Qualify actual native removal separately from
synthetic fault requests.

Do not equate captured/written/durable counts. Verify the entire durable prefix
against an independent delayed signal, recover to a new asset and verify again,
retain original hashes/origins, and attach with the desired musical alignment.
Keep common output-prefix evidence separate from an unqualified post-terminal
sink suffix or full uninterrupted playback.

A failing writer can reject a callback after its asynchronous failure becomes
visible. Permit this only on the initiating lane, report the count, and require
the matching retained callback diagnostic. Preserve the original combined-assertion
failure without retrospectively assigning its unrecorded cause. Short fault
workflows do not qualify sustained, physical, Windows or full reference parity.
