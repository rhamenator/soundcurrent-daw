# Native playback of an admitted, reopened take

Checkpoint 239 verifies the owned Linux recording workflow through native playback
of its canonical admitted project. Full professional-suite parity remains active.

## Concrete workflow

The genuine private PipeWire controller fixture records raw PCM, verifies the paired
native evidence, explicitly admits the take at project frame 257, saves and reopens
it, and verifies all eight evidence references. The fixture then uses the existing
PipeWirePlayback/MixPlaybackRun/read-ahead/shared-engine path with an ephemeral
track EQ adjustment of -6 dB. Canonical project state is not changed by this preview.

Offline export produces an 8,192-frame Float32 reference using 113-frame blocks.
An explicitly selected owner-only dummy sink captures native playback in fixed
256-frame callbacks. Comparison uses the actual playback graph origin, requires
all selected frames once, verifies 257 exact leading zeros, and compares every
sample against offline output. Float headroom is required in the reference.

The current run matches all 8,192 frames exactly (maximum error 0), reports zero
missing frames, zero callback allocation/free/blocking-lock audit hits, completes
native playback, and joins its reader and SDK owners. The recording controller is
explicitly closed before successful fixture cleanup. Graph positions in this test
are playback evidence, not inferred recording alignment or hardware acoustic delay.

## Qualification and limits

Source da478ce binds 839 selected source/checkout inputs. Fresh 144-step headless
Release build and owner-only private PipeWire 1.6.2 run pass. The recording portion
preserves 99,072 bit-checked raw frames, queues/delivers 8,192 monitoring/sink frames,
and retains 387 capture and 408 render receipts. No physical devices, default-output,
volume, user playback, VM or equalizer repository changes occur.

The private route fixes its quantum to 256 frames; an unexpected geometry, missing
or duplicate comparison frame, underflow, callback fault or sample mismatch fails
the acceptance gate. Inactive unmapped link priming is skipped; every required
played frame remains mandatory. Existing mixer/playback warnings are retained.
This is Linux native evidence; Windows native playback of this portable project,
installed packages, sanitizer qualification of this fixture, physical audio and
remaining alignment/hotplug/recovery workflows still need independent evidence.

Original recording-queue failures remain unresolved. The measurement revision
PR172 run 38332233927 is kept intact; inspect its original timing/project evidence
before cadence or production correction. Continue Windows portable take playback
and remaining M2 workflow qualification, then later staged milestones.
