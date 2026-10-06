# ADR010: asynchronous playback and canonical-model audition

Status: accepted for S6e foundation,2026-10-05. [Contract and limits](../20-desktop-playback.md).

Use a separate desktop playback QThread rather than performing native/media preparation, joins or endpoint methods in widgets or the canonical project/I/O workers. Reuse `PipeWirePlayback` and the exact shared EQ ingress; optional injected endpoints make control/UI failure cases deterministic without implying native qualification. Native signal acceptance uses the real window, worker and owned independent sink.

Keep a bounded prepare/play FIFO, priority stop epoch/close flag, one immutable latest read model and one explicitly coalescing canonical-model slot. Preserve gesture/history in the project controller; treat the transport as current-state audition, not automation capture. Submit one prepared EQ bundle at a time, retain partial Full suffixes, and distinguish desired, fully accepted and audio-applied model revisions using exact event receipts. Structural changes require a fresh generation.

Require explicit complete output selection and validate live descriptors at activation. Keep a completed silent owner connected until Stop/reprepare/close to avoid destroying a final output link before downstream consumption. Normal close waits asynchronously for both workers; a blocked read or join retains its resources and visible pending state.

This does not deliver recording, export, native Windows, complete undo, persistence of routing, seamless graph replacement, hardware/PDC/latency or frozen-reference parity. Existing native timeout/module-unload gates, localization and accessibility qualification remain open.
