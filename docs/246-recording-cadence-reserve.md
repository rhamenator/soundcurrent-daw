# Recording fixture cadence and declared manual reserve

Checkpoint 246 continues recording qualification. The full professional suite,
platform installers, physical audio timing and reviewed European localization
remain incomplete.

## Work-aware simulated device cadence

The fake native SDK previously spent time servicing capture, disk/processing
feedback and rendering, then always waited another 10 ms. Each iteration still
advanced its declared audio clock by only 10 ms. That additive delay reduced
virtual-clock progress within the unchanged five-second owner observation.

`NativePollWait::waitUntil` now waits only the remainder of a service period.
Past deadlines return without an OS wait; an interval above one second is
refused. The prepared Windows high-resolution timer and Linux sleep remain
outside audio callbacks. There is no spinning, missed-cycle replay, global timer
resolution change or alteration to production workers' existing `wait()` calls.
The fake SDK starts each period at its actual current time, services real owners,
and waits until that period's start plus 10 ms. Work overruns remain possible.
Nominal packet sizes, sample clocks, fit-confidence requirements and deadlines
are unchanged. New diagnostics distinguish maximum cycle work, cycle elapsed
and pacing wait spans.

The standalone cadence test performs work before waiting, verifies the requested
period has ended, and checks past-deadline and unbounded-interval behavior. It
records observations without asserting a tight wall-clock upper bound.

## Existing manual recording reserve

The desktop already has a **2–20 second** reserve control, defaulting to 10 seconds.
Preparation already calls `withCaptureReserve`. No duplicate control or product
default is added. The API retains the bounded 256-slot maximum and explicit
memory admission. An incompatible small slab geometry can still be refused.

The concurrent-owner test now explicitly admits a two-second profile with the
same 256-frame maximum callback and product-style default slab resolution:
3,000 frames per slab, 32 slots, 96,000 frames at 48 kHz. Its twenty takes,
continuous EQ oracle and original five-second control observation remain.
This is an explicit fixture policy change, not proof that the earlier
8,192-frame fixture could tolerate the same stalls.

Two new tests hold the real disk writer at its first audio write while the
independent duplex stream runs at its declared sample-clock cadence:

- Within capacity: 48,128 captured frames, complete punch-out and exact raw data.
- Beyond capacity: `CaptureFailed` / `QueueFull`, precisely 96,000 accepted frames,
  incomplete group, joined finalization and bit-exact accepted prefix.

The held acquisition records 3,000 frames and 96,000-frame capacity. The retained
small-pool tests still prove exhaustion and an exact 8,192-frame prefix. No global
pool enlargement or suppression of capture failure is introduced.

## Acceptance and retained failures

Code source: `aa04ef6929abc81db922e79ee34e3807b4c77659`.

| Release gate | Result | Time |
| --- | --- | --- |
| Native service cadence | Pass | 0.01 s |
| Native storage and finite-output workflow | Pass | 8.83 s |
| Composed native startup/controller/recording | Pass | 19.28 s |
| Final manual recording workflow | Pass | 4.27 s |

The first three results are from the initial selection; their dependent sources
remain unchanged. The final manual executable was rebuilt after fixture repairs.
Measured held-writer ages were 973,083,038 ns within capacity and 2,001,824,913 ns
beyond it. These are local fixture observations, not guarantees of device latency,
filesystem syscall time or scheduler responsiveness. Full callback allocation,
free and blocking-lock audit remains zero. No physical endpoint or VM was used.

Two new-fixture failures remain retained. The first omitted control servicing,
so no writer started before the original five-second observation expired. The
second fed the duplex stream in an accelerated loop and failed its `Running`
assertion after 19,712 captured frames. Its finalized journal reports capture
status 1/end reason 1; the callback status was not recorded, so a precise cause
is not asserted. The repaired fixture services the control owner and follows the
sample-clock cadence already used by the concurrent-owner test. Original logs,
project metadata and hashes remain available; actual failed project audio is
retained locally.

The [evidence index](../tests/results/M2/2026-10-13-recording-cadence-reserve/index.json)
contains 65 compressed text/configuration/metadata members, 770 source-input
bindings and four executable hashes. Source bindings retain both Git and checkout
hashes, including the existing PowerShell CRLF checkout conversion. No WAV,
executable or original ZIP payload is published.

## Independent preceding-source Windows qualification

[Original PR179 run](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38343777073),
source `daea8d16960cdb70ac18d74c691aef36f93d04ef`, completed successfully:
Linux 158/158 in 308.94 s, Windows native 85/85 in 269.47 s, Windows desktop
20/20 in 80.50 s, and Windows core cross-build pass. Native finite-output,
saved receipt and original-export gates passed on MSVC. Its separate twenty-take
concurrent-owner diagnostic also completed. Original job logs, selected metadata,
and local Windows ZIPs were retained before further source publication.

That preceding source does not contain this cadence/reserve change. Its success
also does not erase the original PR178 96 kHz clock-progress failure or PR176
manual 8,192-frame queue exhaustion. Those original failures remain retained.
New-source Windows CI and real device qualification are separate requirements;
no deterministic repair of every prior timing failure is claimed.

## Next concrete work

Retain the original new-source CI terminal result and inspect any new cadence
measurements without increasing the five-second gate. Continue interrupted-original
verified copy/review, independently test native Windows playback when resources
allow, and finish the remaining recording and full-suite milestones.
