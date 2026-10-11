# ADR114: preserve and correlate Windows capture/render clocks

Status: accepted implementation direction; native duplex is not implemented or
qualified by this decision. References checked2026-10-11. Builds retain the frozen
product baseline; this is an implementation boundary, not a new parity baseline.

## Evidence and existing owners

Capture packets report a frame position and a correlated timestamp in100ns QPC
units. Their lease must be acquired/released on one thread. Invalid timestamps
and discontinuities require explicit handling, rather than substituting a render
position. [Microsoft GetBuffer documentation](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).

Render clock positions have stream-dependent units and require their reported
frequency to convert to seconds. QPC observations are also in100ns units; delayed
readings can be marked S_FALSE. Neither queued sequence nor software callback
position proves audible device position. [Microsoft GetPosition documentation](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition).

The nominal frequency does not account for drift/jitter relative to QPC. Separate
endpoints cannot be assumed synchronous merely because both advertise48kHz.
[Microsoft GetFrequency documentation](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getfrequency).

Existing WasapiCaptureStream retains packet metadata after SDK release;
WasapiRenderStream distinguishes submitted/content/startup/end guard frames,
native clock units/frequency/QPC/padding. PreparedWasapiInput currently drives
single-track AudioBridge; PreparedWasapiOutput drives MixPlaybackRun. The Linux
DuplexBridge assumes one shared DeviceBlockClock and cannot directly receive two
uncorrelated WASAPI clock positions. This conclusion follows from their APIs and
current code; it is not hardware qualification.

## Implementation order

1. Add a framework-free, bounded clock correlation primitive: typed native-frame,
   raw render-tick, QPC and project-frame domains; generation/endpoint identity;
   checked integer/fractional conversion with declared rounding/error bounds.
   Handle unreliable/absent readings, startup-zero observations, backward/reset
   clocks, overflow and excessive drift without fabricating an origin. Test
   44.1/48/96kHz, byte-like render frequencies, unrelated origins, ±clock drift,
   long sessions and all timestamp/refusal boundaries under RT audits.
2. Add a charged, preallocated stamped capture bank and bounded SPSC handoff.
   Capture callbacks publish complete copied packets plus ORIGINAL metadata;
   render/processing ownership consumes causal input at an explicit monitor
   delay. Overflow/underflow/loss retires the run or records an explicit gap;
   neither owner waits, allocates, performs disk I/O or silently drops data.
3. Reuse/evaluate the existing prepared resampling primitives for bounded drift
   correction in monitoring. Preserve original raw capture and durable clock
   mapping separately. Timeline alignment and correction printing require
   explicit policy and acceptance; an adaptive monitor buffer is not proof of
   correctly aligned raw takes. Persist versioned mapping without weakening
   existing loop-context validation or inventing a common native clock origin.
4. Compose the native capture/render owners with in-process playback/EQ, raw
   writers, finite/punch/loop/Stop postroll and original-context publication.
   Quiesce/join native owners before finishing writers or retiring banks/DSP;
   reader failures, loss and partial activation preserve original recovery jobs.
5. Bind RecordingController's Windows duplexFactory and qualify actual desktop
   native routing, monitoring, scalar edits, retention/history/save/export on
   owned Windows endpoints. Simulated Windows CI and cross-compilation remain
   separate evidence. Hardware/installed and sustained-load gates follow.

No dependency, driver or signing certificate is introduced by the first primitive.
SDK interfaces stay in native adapters; engine state and clock metadata remain
framework-independent. All Windows functional workflows remain required, including
multi-input recording and loop capture. This decision does not mark them complete.
