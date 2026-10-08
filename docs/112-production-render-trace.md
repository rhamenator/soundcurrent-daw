# Actual production EQ lease samples around Stop

PR #54 merged at `32e6f8161d988aadd04524d29f26ecad8019a7e6` after all protected
checks passed. Its hosted MSVC 19.51.36260.0 run executed seven selected tests
out of 38, including the actual playback control owner with SDK preparation
injected. This fixes unused output memory credit after refusal; it does not fix
or qualify native EQ cancellation.
[Exact hosted log and receipt](../tests/results/X007/2026-10-08-hosted-output-rollback/receipt.json)
retain that earlier source identity separately from the new trace implementation.

## Diagnostic implementation

The optional `WasapiRenderTrace` is disabled by default. Control constructs a
fixed sample bank and row bank before native preparation. A mandatory resource
ledger admits the object and both banks together; at most 2,048 rows and 8,388,608
float values (32 MiB of samples) are retained. Capacity and format mismatches
refuse before activation. It cannot be copied, moved, reset or reused.

The real production renderer copies **actual interleaved SDK lease contents**
after the production graph/EQ callback and before ReleaseBuffer. The row records
the actual release HRESULT and argument after the call. A failed acquisition has
no fabricated release. An Abort records zero released extent and never reads
potentially unwritten SDK backing. Its engine position can still differ from
committed native frames; those domains remain separate. A copied lease with a
failed release is retained as rejected, rather than counted as committed audio.

Only content leases are sampled. Native startup and end-guard silence retain
their separate existing counters. Timing fields preserve SDK stream clock units,
SDK QPC timestamps in 100 ns and queue-frame extents. This trace is a sample
diagnostic, not a DSP CPU benchmark or a physical-latency measurement.

One native writer appends into fixed storage. Rows and samples are read only on
control after stop/join seals the bank. There is no concurrent reader, queue wait,
allocation, retirement, log, GUI work or disk write in the append/release path.
Overflow exposes lost rows/sample frames and cannot qualify a complete trace.
Sample copying itself extends a render lease, so native observations must retain
that instrumentation scope. Default-disabled operation has no sample copy.

## Fixture and independent analysis

The existing production playback fixture now shares a 64 MiB resource ledger
between graph, reader, output and trace. Its source, EQ, actual applied-frame
receipt, output channel mapping, Stop behavior and existing sample acceptance
thresholds are unchanged. After joining, it writes `render-trace.json` and
`render-lease-samples.f32`, alongside the original raw take, loopback and engine
reference. Failed native outcomes still retain their actual statuses.

`tests/analyze_windows_render_trace.py` independently evaluates the original
live EQ recurrence at the actual receipt frame, checks every copied sample and
unselected channel, and accounts for release success, Abort and failed acquisition
without inventing submitted audio. It separately compares the observer with the
committed actual lease samples. Interior fitting finds alignment only; original
unity-gain residuals decide fidelity. A missing queued extent and an altered
observed tail are reported separately. It never promotes endpoint or full playback
qualification from a diagnostic pass.

The SDK-free C++ tests verify owned backing after simulated release poisoning,
float headroom, raw errors/timing, zero-copy Abort, loss, mismatched preparation,
project-wide admission refusal, incomplete/double release, retirement and real-time
allocation/lock auditing. Synthetic Python controls reject eleven metadata
mutations and distinguish changed bank samples, failed release/acquisition,
Abort, faded/scaled observer tails and missing final extent.

Linux Release, ASan/UBSan with leak detection, the synthetic analyzer and MinGW
cross-build pass. Hosted native MSVC execution of this new trace case is pending;
the required job now compiles the actual production playback fixture and runs
the SDK-free trace unit test. **The fixture itself is compiled, not automatically
activated by CI.** No new native endpoint or installer evidence exists yet.
[Local source/test receipt](../tests/results/X007/2026-10-08-production-render-trace.json)
records actual exits and exact source/binary hashes within those scopes.

## Next bounded native task

Keep the full frozen-reference goal active. Finish independent disk equality
before using the newly copied clone. The existing verifier remains suspended
following the disk-activity report. Follow the owner's latest VM budget: one VM
at a time, brief runs, verifier paused during a VM run, shutdown afterward.
Original VMs, pristine template and equalizer repositories remain unchanged.

In the qualified independent clone, use fresh hash-verified source directories
and the same explicit endpoint. Execute capture admission and the preserved v1/v2
capture comparison, then normal and active-Stop production playback with this
trace. Retain all original exits, failed traces, partial media, process/compiler
identities and independent analyses. Use actual lease-versus-observer evidence
to decide the next correction. Existing preview binaries remain unchanged.
