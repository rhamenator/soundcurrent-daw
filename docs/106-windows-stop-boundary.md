# Direct Windows Stop and non-silent endings

This continues the [startup correction](105-windows-startup-scheduling.md).
The production mixer/EQ cancellation failure remains unresolved. A new direct
renderer experiment narrows its investigation without changing product behavior
or declaring playback completion from queued frames.

## Contract and experiment

Microsoft documents [Stop](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-stop)
as freezing the native stream clock; a later Start resumes it.
[Reset](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-reset)
flushes pending data while stopped. These descriptions do not promise lossless
endpoint loopback observation through an abrupt Stop or identify a fade's cause.
The product owner currently joins and retires the native client on Stop.

The opt-in direct fixture adds two modes on the explicitly selected owned endpoint:

- `cancel-active`: stop after at least 48,000 content frames have been submitted,
  without a mixer, reader or EQ. Require Stopped/not-drained behavior, not normal
  completion. Actual lease samples must match the independent source prefix;
  untouched unsubmitted storage remains zero.
- `non-silent-end`: render all 96,000 source frames, including a non-silent final
  region and sample. A full source extent in the queue is not sufficient proof
  that every source sample reached the observer.

Both use the production renderer's default 480-frame startup interval, preallocated
samples and bounded observations. Processing callbacks contain no added disk I/O,
logging or allocations. Queue extents, content frames, SDK clock units and QPC
remain separate. Control code reads callback state after native threads join.

The independent analyzer checks the float recipe, actual lease samples, channel
silence, finite media, hashes, timing/queue progression and finalized observer
journal. Interior alignment uses nonperiodic noise, not the possibly changed tail.
It reports residuals and unobserved source frames separately. Eight synthetic
cases detect altered Stop tails, truncated non-silent endings, poisoned media,
changed leases and invalid timing/cursor metadata. These are diagnostic checks,
not relaxed fidelity acceptance thresholds.

## Results

One owned independent Windows development clone, no SPICE viewer, 48 kHz stereo
endpoint; default/output/session volumes unchanged. MSVC Release builds and three
limited interactive session-1 runs exit zero with the same executable:
373,248 bytes, SHA-256
`cc596dfe47227e1d5326a5527654fb6ad1a826607ea79b92df32b854311212dd`.
The 344 frozen source inputs match
`df9611d3a0b57ba0848a7839def3770e41f54064`.

| Mode | Content submitted | Captured source overlap | Maximum residual | Unobserved submitted source |
| --- | ---: | ---: | ---: | ---: |
| Active Stop, run 1 | 48,480 | 43,616 | 0 | 4,864 |
| Active Stop, run 2 | 48,480 | 43,616 | 0 | 4,864 |
| Non-silent end | 96,000 | 95,936 | 0 | 64 |

All compared samples match exactly at measured path gain 1 and fixture offset
544 frames. Both Stop runs remain not drained. Their incomplete queued extent is
reported explicitly. The normal run reports drained but **does not deliver the
last 64 non-silent source frames to this observer**. It fails full end delivery.

Inference: active native Stop alone did not reproduce the prior production EQ
probe's altered tail in these two runs. This does not establish that the native
path cannot alter a different signal or scheduling sequence. The earlier failure
remains retained, not reclassified. A matched-signal production lease trace and
repeat runs are needed to distinguish application output from downstream behavior.

The initial dispatch observation saw the task still Running after a result file
appeared. The same task was subsequently observed Ready with actual terminal
result 0, then unregistered. No second run was dispatched to replace that
observation. The owned clone is shut down; originals and pristine template remain
unchanged.

## Evidence and next task

[Receipt](../tests/results/X007/2026-10-08-windows-stop-boundary.json) and
[43-payload capsule](../tests/results/X007/2026-10-08-windows-stop-boundary.zip)
retain original media, actual lease samples, process identities/exits, source
hashes, logs and independent analyses. Capsule size: 2,706,189 bytes; no executable,
DLL or credentials. `tools/verify_windows_stop_boundary.py` relocates and
recomputes it without starting Windows or replaying audio. Linux startup/Stop
analyzer checks pass; native application C++ callback allocation/free counts are
zero. C/SDK internals and sustained scheduling remain unqualified.

Next implementation task: define and qualify a bounded native end guard that
does not advance source/DSP/project/receipt frames, distinguishes its queue extent
from content, and retains cancellation/fault/timeout semantics. Repeat the exact
non-silent ending and shorter-range cases before installer refresh. Also trace
the actual production EQ lease samples around active Stop and retain any failed
repeat. Do not change the sample oracle or call a drained queue proof of complete
source delivery.

Installed previews remain their earlier builds. Physical audio, capture-start
discontinuities, monitoring/duplex, rate conversion/drift, language/release gates
and full frozen F/Q/C/N parity remain open.
