# Prepared monitor drift controller

`PreparedMonitorDrift` now supplies generation-bound, bounded-ratio events for
the causal monitor buffer. Original raw takes and native timestamp metadata are
unchanged. No Windows duplex UI is enabled by this component.

## Contract

One processing owner receives copied original clock readings, proposes an event
at the next64-frame monitor grid, enqueues that exact event, and commits before
any buffer service. Preparing a proposal does not advance the grid, filtered
occupancy or last scheduled ratio. Exact-point retries are stable; mismatching,
duplicate and pending-point replacements are refused. Queue refusal must retain
both the unadvanced buffer and proposal or fault explicitly. No other thread may
call this object concurrently.

Capture frames and render ticks/frequency have separate identities and clock fits.
Two overlapping windows per endpoint prevent observation-window warmup from
making the active estimate stale. Each rotation seeds from a retained original
reading and preserves first-fault and initial-reading evidence. Large unsigned
origins are subtracted before conversion; output ordinals above2^53 stay integer.
The computed floating ratio is a control estimate, not an exact alignment map.

Declared endpoint timestamp uncertainty is propagated independently. WaitingClock
and WaitingConfidence emit no nominal substitute. Stale estimates, reset,
discontinuity, identity/frequency changes, out-of-range relative drift, control
time reversal, counter overflow and rejected events retain a sticky first fault.
Initial waiting is separate from losing clock fits after processing starts.

Filtered proportional source-ring feedback has explicit feedback/ratio/slew
limits and reports saturation. It does not silently clamp bad clocks into
plausible estimates. Source-ring occupancy excludes the persistent kernel's
internal history and output/hardware queues. Default gain/smoothing and analytic
reservoir tests are provisional; callback partition, real jitter/load, startup,
quality and native clock-control behavior still need qualification.

The controller reserves4KiB before prepared use. Configuration, shared-ledger
admission and joined retirement stay off RT. Prepared methods use fixed POD state
and bounded arithmetic; no allocation, blocking lock, SDK, logging or disk work.

## Frozen evidence

Source `5935e92bd67d7e453a9f78524b4ffe3e58f822b6` passed the Linux Release
controller in0.20s and ASan/UBSan controller/clock suites in2.54s with leak and
halt-on-UB checks. Full C/C++ TSan controller execution passed0.92s. This is
single-owner execution/profile compatibility, not a concurrent-owner race proof.
Each controller execution made4,088,610 checks, including:

- 225 independently generated Fraction ratio/uncertainty cases across8–384kHz,
  differing native render frequencies and unequal original observation spans.
- Two500,000-grid analytic reservoir simulations at opposing endpoint drift;
  default policy settled±200-frame initial errors below one frame. Each simulates
  about11minutes, with repeated overlapping-window rotation. This is a fluid
  model, not actual callbacks, disk behavior, physical latency or native stability.
- An actual8,192-output-frame stereo causal buffer run with source packet257,
  pull7, output ring129 and retained source128;1,154 bounded service invocations,
  finite actual output and exact Stop. No spectral/phase/headroom-quality claim.
- Generation, age, uncertainty, clock loss/reset/discontinuity/frequency,
  large ordinals, overflow, source occupancy sign, queue pressure/identical retry,
  invalid/duplicate commit, explicit feedback/ratio/slew saturation, credit
  refusal/retirement and first-fault preservation.

Guarded paths had zero C++ allocation/free and zero Linux-interposed libc
allocation/free/blocking-lock counts. Interception does not cover every library
or syscall. Windows MSVC target/suite is added but its execution is independent.

The [29,681-byte packet](../tests/results/M2/2026-10-11-monitor-drift-control/summary.json)
retains15 members,553 selected source hashes, four executable hashes, incremental
build/CTest/LastTest logs, configured C kernel commands and a fresh frozen Release
compile/link transcript. The fresh controller/clock/test compilation reproduced
the exact executed binary hash; no redundant replay was needed. Existing unchanged
C kernel objects were reused and their library hashes retained. Prior kernel/profile
compilation evidence remains in checkpoints182/183. This is not a fresh complete
toolchain/system/dependency build. Standalone decompression and selected byte checks
passed before later prerequisite inheritance. No executable/media is included.

## Next implementation

Compose the original stamped bank/raw-writer route with processing and prepared
output SPSC queues. Validate controller/buffer admission together, make clock
observation-before-monitor activation explicit, and implement native capture/
render owner shutdown and fault retirement. Preserve raw prefixes and versioned
raw-to-project correspondence separately from monitoring. Stop/punch/loop real
postroll and native Windows duplex desktop binding remain open.

This is component progress in P005/M2. Native/installed/physical/load and quality
gates, earlier DSP quality failures, M2–M11, X004/X005 and full functional,
processing-quality, content, compatibility and European localization are incomplete.
