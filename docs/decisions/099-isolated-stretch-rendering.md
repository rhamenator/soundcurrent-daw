# ADR099: independent stretch in an isolated worker

2026-10-09. Status: implemented CLI foundation; application/platform gates open.

Select pinned Rubber Band4 R3 offline in a separate worker. Its variable output,
internal allocation growth and stateful phase/context make direct callback calls
inappropriate. Use OS memory containment and a process deadline, with canonical
integer output duration verified before completion. This is not a privilege sandbox.

A completed floating derived asset can be read by the existing framework-free
live/offline readers. Source-span/content/settings keys preserve source anchoring
across seek/split/crop. Parent supervision and versioned editable anchor state are
required before desktop integration; a bounced asset alone does not prove editing
parity. Preserve raw media, inspect incomplete jobs, and publish a completion marker
only after verification. Never interpret an existing audio file as a completed job.

Choose the unchanged63-file GPL-or-later subset and builtin FFT/BQ build to avoid
new optional transitive libraries. Root GPL3 distribution retains upstream notices.
Keep384k, broader modes/layouts, full processing quality, formants, cache recovery,
aggregate parent admission, Windows native/installed behavior and parity open.
See [checkpoint134](../134-isolated-stretch-worker.md).
