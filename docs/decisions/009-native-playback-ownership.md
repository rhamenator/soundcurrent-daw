# ADR009: native playback clock and lifetime owner

Status: accepted for S6 foundation,2026-10-05. Qualification limits: [contract](../19-native-playback-owner.md).

The first file-backed native fixture implemented its own clock checks and lifetime shutdown. Keeping that logic in a fixture would leave the desktop workflow without a production owner and let tests validate a path the application never uses.

Move device-clock validation to a backend-free `PlaybackBridge` over `PlaybackRun`. Use a Linux `PipeWirePlayback` control/preparation owner for project verification, inactive filter/explicit links and ordered native/reader shutdown. Keep processing unchanged and share the same EQ with offline rendering. The native fixture uses that owner and an independently captured sink.

Choose explicit, complete output-port selection and stop on identity/rate/clock failures. Do not force graph activity in the production player, change daemon defaults or substitute automatic reconnection. Publish bounded lossy diagnostics and immutable timing origin; retain existing immediate ingress/receipts. Use single-attempt audio state publication so a concurrent control fault wins without an RT retry loop.

This owner may block during preparation or joining and therefore must be called from an asynchronous desktop worker. It does not yet provide GUI playback, seek migration, recording, Windows backend, hardware latency/PDC, memory locking or deadline/load qualification. The observed native timeout and normal dependency-unload memory gate remain visible; passing serial sample comparisons do not resolve them.
