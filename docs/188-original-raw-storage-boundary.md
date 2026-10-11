# Original raw recording storage boundary

This checkpoint freezes a bounded storage observation and the next implementation
contract. A production stamped raw writer is not implemented by this experiment.

## Existing path assessment

`CaptureWriter` already provides exclusive job creation, RF64 float32 writes,
checked audio flush before durable journal publication, sample/file hashes,
interrupted-prefix inspection, inactive-job leases and no-overwrite final media
publication. Reuse those disk/security/durability primitives and their fault tests.
Its input is `CapturePipe`, whose legacy producer replaces nonfinite samples
with zero. Its current journal binds signed engine-frame starts, not the original
typed native packet/QPC sequence. Feeding new raw bank packets through that pipe
would lose original exceptional bits and substitute an unsupported timing model.
Do not construct fake engine frames or QPC nanoseconds to make that API fit.

The new disk consumer must accept the original raw bank view directly, while
monitoring has its own finite-input and failure policy. Whole original packet
copies and stamps precede any conversion. Original float32 nonfinite samples
may be preserved as fault evidence; they do not become valid playable media.
Existing finite media/recovery validation must not be weakened. Define a separate
verified original-bit prefix and a finite playable/admissible prefix explicitly.

## Bounded RF64 experiment

An owned temporary Linux file was exclusively created with no-follow semantics
and mode0600, then written/read through the existing libsndfile1.2.2 RF64 float32
API. Eight stereo frames retained all16 tested bit patterns: signed zero,±1,
finite headroom, maximum finite values, subnormals, infinities and quiet/signaling
NaN payloads. The file was flushed/closed and removed after the run. No device,
native callback, GUI, user audio or equalizer was involved.

The [1,780-byte evidence packet](../tests/results/M2/2026-10-11-stamped-raw-feasibility/summary.json)
retains four members: original GPL experiment source, build/run logs and source/
executable/header/runtime hashes with compiler/command identification. No executable
or media is included. This is one Linux runtime/format observation, not general
codec, endian, Windows, durability-interruption or complete toolchain qualification.
It supports examining RF64 reuse; it does not prove a production raw writer.
No dependency was added; the existing libsndfile license/provenance remains applicable.

## Next implementation task

1. Introduce a separately versioned original capture context/journal. Bind project,
   track and asset identity, format/layout, original endpoint/stream/generation,
   typed native frame/QPC units, declared input latency and actual raw-file extent.
   Nominal sample rate is not proof of synchronization with the project clock.
2. Append complete original packet records with exact raw-file offsets, native
   frames, QPC100ns values, flags and sequence. Use checked integers above2^53/2^63,
   bounded records/pages and incremental integrity checks. Recording length must
   not require an in-memory metadata array proportional to the entire take.
3. Flush audio and original metadata before publishing their common committed
   prefix. A crash between either write/flush/journal/final publication must leave
   only a verifiable earlier common prefix eligible for recovery. Never publish a
   longer audio extent as synchronized metadata, or rewrite original evidence.
4. Share existing disk file/job lease/hash/no-overwrite primitives; distinguish
   legacy journals and unsupported versions, reject duplicate/overflow/malformed
   identity/extent records, and preserve originals on invalid metadata. Optional
   alignment metadata failure must not destroy otherwise intact raw audio.
5. Keep raw-file frame indexing separate from exact raw-to-project correspondence.
   Persist trustworthy original clock observations and explicit declared latency;
   do not use monitor resampling/opaque output ordinals as recording alignment.
   Only validated mappings may attach material to finite/loop/punch project views.
6. Qualify original-bit and finite media policies, Unicode paths, partial/faulted
   prefixes, no-overwrite, copied/recovered identity, clock changes/loss, durable
   paired-prefix interruption and independent Windows execution. Then compose
   the disk consumer with capture fanout, DSP/controller and output transport.

All disk work and parsing stay on disk/control owners. Capture/processing/render
remain bounded; they do not wait for journal durability. Stop/fault must join native
owners before final metadata settlement/retirement. Installed native duplex,
processing quality, full P005/M2 and broader DAW parity remain unqualified.
