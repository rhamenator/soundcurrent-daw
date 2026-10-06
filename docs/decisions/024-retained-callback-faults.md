# ADR 024: retain first callback fault independently of lossy meters

Date: 2026-10-06. Status: accepted for M2c4.

Keep one immutable fixed-size callback validation record in the framework-free
playback bridge, published by its single audio writer with a release/acquire
flag. Forward/copy it through the native owner and control-worker snapshot before
retirement. Generation preparation clears the current snapshot; old snapshots
remain immutable. The detected callback reason does not override a concurrently
winning terminal reason. Control-originated faults need not contain clock facts.

A lossy meter queue can be full when the fault occurs, and endpoint retirement
would otherwise erase the rejected clock. Callback logging or a dynamically sized
history would violate the real-time contract. One retained record supplies the
facts needed to distinguish rate/quantum/buffer/clock failures without either
mechanism. Backend-free tests verify meter overflow, silent refusal, unchanged
cursor, later-callback immutability and control retirement/generation behavior.

Measure callback elapsed time only in opt-in owned Linux fixtures, using fixed
storage and post-join inspection. Preserve failed evidence, scheduler observations
and cleanup without suppressing failures. Finite timing observations are not
deadline qualification or an explanation of historical failures. See
[the contract and next task](../34-native-timing.md). No dependency change.
