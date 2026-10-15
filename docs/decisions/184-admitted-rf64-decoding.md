# ADR 184: native integer loads within admitted WAVE byte ranges

Status: implemented; full workflow timing qualification pending.
Date: 2026-10-14.

## Measured problem

The paired-history optimization alone did not meet Debug deadlines. At source
5b67777, both the original version-two worker suite and the new paired-history
suite exceeded 180 seconds. Repeated production completed its first generation,
then a later worker reached its unchanged ten-second deadline.

A bounded Callgrind experiment on one original 96 kHz history captured
13,852,789,990 instruction references before instrumentation overhead caused the
original assembly deadline to reject the review. This is a partial profile, not
a production timing or a successful review. In that captured prefix, the byte
integer loop and its repeated span size/index calls account for approximately
44% of self instructions. The positioned resampling tap loop accounts for 6.69%.
Preserve the original failures and profile, rather than inferring that the FIR
kernel is the primary bottleneck.

## Decision and boundaries

Decode native-order 16-, 32- and 64-bit unsigned integers with fixed-size memcpy
loads after the caller has admitted the full byte extent. This permits unaligned
input without typed aliasing. Opposite-endian, mixed-endian and other-width inputs
use the original byte-accumulation algorithm, with pointer and size captured once.
Headers retain their span wrapper. The RF64 sample loop reuses its admitted raw
and decoded array views, avoiding repeated container access for every sample.

No decoding format, floating-point conversion, source mutation check, sample
finiteness check, peak computation, byte/I/O limit, resource reservation or
observer boundary changes. The RF64 header independently validates sample width,
channels, block alignment and exact data extent before decoding; each block
fits the existing slabs. All samples still participate in verification and the
complete original file is still hashed.

The existing approved-wave-content corpus covers RF64 PCM8/16/24/32 and
Float32/64, independent exact sample expectations, floating headroom, opposite
endian headers, malformed/truncated containers, NaN/Infinity, cancellation,
resource limits and complete resource retirement. It passes with the initial
optimization in Debug. Full repeated-history and worker timing qualification,
unchanged export hashes, and native Windows execution remain required. Test and
worker deadlines are not increased.
