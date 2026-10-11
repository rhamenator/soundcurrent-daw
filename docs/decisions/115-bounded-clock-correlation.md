# ADR115: bounded native clock correlation core

Status: implemented core; Linux and independent Windows MSVC synthetic execution
have evidence in checkpoint179. Native integration remains a separate gate. Parent direction: [ADR114](114-windows-duplex-clock-boundary.md).

## Decision

Use original GPL-3.0-only, framework-independent C++20 code with typed capture
frames, native render ticks, and 100ns QPC timestamps. Endpoint, stream and
generation tokens are admitted off the real-time path. A run-generation token
binds an explicit QPC/project anchor. Separate device owners get separate fits;
the primitive does not turn independent clocks into a shared DeviceBlockClock.

Render units must follow the reported frequency. Delayed readings do not seed or
refresh a fit. An initial render-zero plateau cannot seed it. Capture frame zero
is allowed when its original timestamp is valid. This policy follows the native
API contracts checked 2026-10-11: [GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition),
[GetFrequency](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getfrequency),
and [capture GetBuffer](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer).
Skipping an unreliable reading here does not override a native owner's terminal
timestamp-error or packet-loss policy.

An affine fit uses original endpoints within a bounded observation window.
Nominal frequency checks both successive readings and the whole candidate fit,
so a two-reading jitter allowance cannot accumulate without limit. Excessive
drift, a new identity/frequency/unit, backwards clocks, later discontinuities and
arithmetic overflow cause a sticky first fault. A long trustworthy gap retires
the old fit and requires new observations. Missing/delayed observations preserve
the prior fit only for its original bounded age. No retry/spin loop is introduced.

## Arithmetic and ownership

Subtract unsigned native origins before conversion; do not cast large absolute
positions to double. Implement checked 64-by-64 multiplication followed by
128-by-64 division using standard unsigned limbs and a bounded 64-iteration
division. This avoids a compiler-specific integer extension and a new dependency.
Floating-point conversion was rejected because exact offsets above 2^53 would be
lost. Dynamic arbitrary-precision arithmetic was rejected for the real-time path.
An independently generated Python Fraction table checks the original algorithm;
Python is a development/test oracle, not an audio runtime dependency.

Project QPC to the nearest integer, choosing the earlier absolute timestamp on
ties; convert its difference from the explicit project anchor into an exact
signed floor plus a fraction with denominator 10,000,000. Refuse values outside
the signed Frame range, including a fraction above Frame::max. All extrapolation,
fit-age, generation and overflow checks are explicit. The error budget contains
declared timestamp uncertainty and integer rounding only; it excludes hardware
latency, actual clock-model residuals, scheduling and physical calibration.

One owner mutates each prepared instance. Construction may throw and happens off
RT. Observation, projection, fault/snapshot access contain no allocations, locks,
I/O, logging, SDK calls or unbounded iteration. Snapshots are trivially copyable
original endpoint observations; they are not a concurrent-reader guarantee.
Cross-owner delivery still needs the charged bounded handoff in ADR114.

## Consequences and next gate

This core changes no audible workflow, raw media, project schema or native owner.
It adds no library, driver, signing certificate or installer requirement. Local
acceptance and independent Windows MSVC execution have distinct evidence scopes.
Neither qualifies native capture/render synchronization or Windows loop recording.

Next: implement the preallocated stamped capture bank and bounded SPSC delivery,
retain original packet positions/QPC/flags, then add causal monitor delay and
prepared drift correction with durable versioned mappings. Compose SDK owners
before binding the Windows desktop duplex factory. Preserve original raw takes;
monitor resampling cannot be treated as evidence of aligned raw capture.
