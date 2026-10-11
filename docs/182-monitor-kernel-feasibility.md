# Bounded adaptive-monitor kernel feasibility

This is a synthetic dependency experiment and an implementation decision. The
production causal monitor, clock/buffer drift controller and Windows duplex
workflow remain to implement. The existing fixed worker resampler is unchanged.

## Question and method

Can the pinned persistent best-sinc converter process small live input partitions
without allocation/free/blocking-lock calls, while preserving a declared rate
trajectory independently of input packets and output capacities?

The original GPL harness prepares state/finite signal/output buffers outside its
audit. It supplies actual input with `end_of_input=0`, preserves float headroom,
and requests bounded output until the declared extent is reached. Every call's
consumed/generated counts are checked; no-progress and total-call budgets fail
explicitly. No converter reset, end padding, native endpoint or fabricated timestamp
is used. Auditing surrounds only `src_set_ratio`/`src_process`, not a production
owner or its admission/retirement.

The control schedule varies between nominal ratio offsets of−150,−50,+50,+150ppm
on a64-output-frame grid. It is an experiment, not a qualified smooth controller.
1/2/3/8 channels cover48→48,44.1→48,48→44.1 and96→48kHz. Each compares a2048-input/
64-output reference with1/64,17/7 and257/1 partitions. A256-channel case compares
2048/64 and17/7 partitions. Headroom/finite samples and output extent are checked.

## Observations

Explicitly setting the ratio throughout each output-grid interval produced exactly
matching float samples across these partitions: worst absolute difference0.
Using the dependency's automatic transition with changing call capacities produced
a worst difference0.105524 between two schedules requesting the same target grid.
These are sample differences between schedules, not measured distortion figures,
professional quality thresholds, or a vendor defect claim. The automatic ramp's
dependence on requested capacity is visible in the pinned source; our control grid
must own the rate trajectory. [ADR117](decisions/117-causal-monitor-kernel.md) records
the selected policy and pending production contract.

One-frame input cases first generated output after145 actual input frames for
48→48 and44.1→48,158 for48→44.1, and288 for96→48kHz across the tested channel counts.
These are filter startup observations for this supplied signal/schedule. They are
not physical latency, complete native buffer delay or a promised monitor latency.

## Frozen qualification

Source `2b96daab82c6858f7707780e499f61b80d459266` passed four Linux Release
suites in4.03s and four ASan/UBSan suites in34.29s: the new kernel experiment,
existing prepared resampling, independent timing oracle and complete pinned-source
integrity. Leak detection and halt-on-UB were enabled. The new full C/C++ TSan
profile also passed its single-owner kernel suite in22.13s with halt-on-error.
This last result verifies instrumentation/profile compatibility and one-owner
execution; it is not a concurrent monitor-owner race test.

Each kernel run made228,836 audited calls and3,045,133 checks, including sample
finiteness and call extents. C++ allocation/free and Linux interposed libc
allocation/free/pthread blocking-lock counts were zero. Test-only interception
does not cover every library/syscall path or prove callback deadlines. Windows
compiles report libc interception as unavailable; execution is independently
required despite explicit inclusion in the MSVC target/allowlist.

The [43,992-byte packet](../tests/results/M2/2026-10-11-monitor-kernel/summary.json)
retains15 members: build/CTest/LastTest logs for the profiles, actual C kernel
compile commands,539 selected repository inputs, the complete96-file upstream
manifest, five executable and three kernel-library hashes. Executables/libraries
are hashed, not included. Actual TSan C commands contain `-fsanitize=thread` and
exclude ASan; ASan/UBSan commands contain their matching profile. All selected
inputs matched the frozen source. The source-integrity gate verified10,352,083
unmodified upstream bytes. This is not an exhaustive compiler/system/other
dependency closure. Standalone archive/member hash validation passed.

Reproduce with `cmake --build <configured-build> --parallel 2 --target
sc-monitor-kernel-tests sc-resampling-tests`, then `ctest --test-dir
<configured-build> -R '^(monitor-kernel-feasibility|prepared-resampling|prepared-timing-oracle|resampler-source-integrity)$'
--verbose --no-tests=error`. Use a separate TSan configuration; the build forwards
the actual parent sanitizer profile to the unchanged C dependency.

## Concrete next implementation

Implement a charged single-owner causal converter/buffer around the persistent
kernel. Retain real input, phase and a64-output-frame control grid; admit bounded
ratio events, copy/service extents and resource credit; report NeedInput/NeedOutput
without waiting or inventing live future samples. Test warmup, input/output back
pressure, underflow/overrun, ratio/generation faults, Stop/cancel/tails and joined
retirement. Then add/qualify the clock/buffer controller, versioned original
raw-to-project mapping and native SDK composition before enabling Windows duplex.

No audible UI or raw/project schema changes. Full drift stability, spectral/phase/
transient quality, extreme rates, scheduling/load, native/installed/physical/
Windows/localization and full P005/M2/F/Q/C/N remain open. Earlier quality failures
are not closed by these kernel observations.
# Independent Windows kernel execution

MSVC job114401243742/run38116107337 passed55 synthetic suites173.64s, including
the kernel probe0.65s. Its original checkout3d04aa86382b9c343a3c040ab6403f4867bea855
has parentsa40d23d/769dcec and the exact reviewed head tree. A
[54,839-byte/three-member packet](../tests/results/M2/2026-10-11-monitor-kernel-windows/summary.json)
retains the full raw job, original API status/commit binding and nine reviewed
changed-file hashes. The source hashes were checked against the frozen Git blobs.
The successful log exposes no per-probe check count or executable hash, so none is
claimed. This is independent Windows kernel feasibility execution, not native
duplex, installed audio, new controller qualification or current-MAIN admission.
