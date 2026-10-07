# ADR059: certified native buffer leases

Status: implemented scoped Linux checkpoint, 2026-10-07.

The controlled original47 demonstrates an allocated output buffer queried before
IO publication can retain a one-cycle offset. Replace the production convenience
getter with public readiness-gated dequeue, certified mono F32 views and explicit
return after processing. Preserve each unavailable channel's logical ordinal.

Prepare port descriptors and lease/view arrays off RT. Publish synchronous IO
readiness through a lock-free atomic pointer, relying on supported SDK per-port
lifetime/serialized replacement, not atomic-pointer reclamation. Refuse
unsupported asynchronous IO and malformed chunks/extents; return every acquired
native object exactly once. Fail the prepared run on layout or queue failure
through its existing bounded fault path and report the native refusal off RT.

Use version2 test-only observations to distinguish logical acquisition from
actual SDK operations and prove extent/ownership correspondence. Do not alter
timing gates, DSP mathematics, declared delays, schemas or hardware defaults.
The three bounded native passes establish startup / Stop / Cancel behavior;
retained repeated-take failure48 keeps sustained performance unqualified.

No dependency or PipeWire fork is introduced. Linux/Windows parity, newer SDK
lifetime/hot-reconfiguration qualification and detailed refusal statuses remain
required. See [implementation and evidence](../73-native-buffer-acquisition.md).
