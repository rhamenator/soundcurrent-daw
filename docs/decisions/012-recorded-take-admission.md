# ADR012: native recording ownership and verified take admission

Status: accepted for S6 foundation, 2026-10-05. Qualification limits: [contract](../21-native-recording-owner.md).

The native capture fixture assembled its own bridge, pipe and writer. Move that lifetime into a production `PipeWireRecording` owner so desktop integration and native tests share the same capture path. Publish inactive explicit input/output ports, create the disk job at activation, and join callbacks before finishing/draining the writer. Monitoring Off has no output port; Post-EQ requires complete explicit output selection. Destruction finalizes; cancellation is a separate checkpoint-preserving action.

Copy raw input before DSP, including aliased buffers. Arbitrate terminal causes with bounded atomic publication; natural range completion never hides a disk result. Keep blocking setup/joins off GUI/audio threads.

Use a typed immutable recording receipt to request project attachment. Validate shape on the canonical control worker, inspect finalized journal and hash media on the I/O worker, then attach to the latest model so intervening scalar edits survive. Compare persisted semantic journal identity, not nonpersistent runtime slab/pool settings. Do not add speculative media or auto-save. Rejected/canceled attachment retains finalized files for recovery.

General clip undo, recording GUI/close choreography, recovery discovery, export, native Windows, hardware alignment and deadline qualification remain required. This decision supplies reusable ownership/admission, not full recording workflow or reference parity.
