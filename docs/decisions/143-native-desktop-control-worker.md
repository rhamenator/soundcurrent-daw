# ADR143 — Native recording controls on an owned desktop worker

2026-10-11. Accepted for bounded engine/controller integration; product panel and
native/installed hardware qualification remain pending.

Use one non-RT control worker owning the actual native recording owner. Expose
immutable snapshots, a 16-command FIFO, urgent cancellation epochs and exact
terminal-result release. Keep device preparation, file work and joins off GUI and
callbacks. Reuse the prepared route and processing/disk workers rather than
creating another callback/DSP path. Prepare from saved project identity; use
explicit native device selection, complete native channels and a finite opaque
monitor end. Parameter admission and applied receipts remain distinct.

Preserve original raw/native outcomes through failures, close and inactive reopen.
Do not attach raw samples to project time without separate finite/audio/alignment
admission. Read mutable worker faults only after acquiring terminal publication;
normal disk completion cannot prematurely cancel monitor drain.

The Windows factory refreshes inventory and explicitly revalidates selected ports.
It remains opt-in until native and installed qualification. The portable controller
and real owned workers are qualified against simulated SDK endpoints. Linux native
factory, panel/localization and aggregate GUI payload admission remain next.

See [checkpoint214](../214-native-monitor-controller.md) for acceptance evidence
and the separate preceding Windows desktop interruption failure. No driver,
dependency, proprietary asset or reduced parity target is introduced.
