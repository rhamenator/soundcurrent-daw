# Bounded Windows capture trace: continuity gate remains open

The local Ubuntu recording preview and Windows installation/UI preview remain
available with their exact source pairs. The refreshed Windows installation
evidence passed protected checks and merged through PR #51. No public binary
release was uploaded. This checkpoint adds diagnostic source and evidence;
the installed preview does not contain this trace.

## Trace ownership and timing domains

`WasapiCaptureOptions` accepts an optional control-owned `WasapiCaptureTrace`.
The default is disabled. Before native preparation, the owner allocates its
fixed 2,048-entry SPSC queue and keeps it alive through stream stop and join.
The native producer publishes POD metadata after release; a control consumer
drains it during a run or after join. Full queues drop metadata and increment
an explicit saturating counter; sequence holes expose loss. No borrowed audio
pointer, retry, disk write, logging, allocation or blocking queue lock is added
to publication. Prepared metadata is immutable; the owner cannot reset/reuse it.

Observations keep device-frame positions and SDK packet timestamps in 100 ns
units separate from raw QPC ticks and their prepared frequency. They retain
wait start/wake, acquisition, callback return, release, actual HRESULTs,
packet flags, catch-up state and ordering. Clock failure remains explicit.
Wait failure's `GetLastError` is saved before the optional QPC call.
Native C++ allocation/free counters stayed zero in the processing windows;
portable queue tests additionally audit libc allocations/locks. These counts
do not establish anything about allocation inside Windows SDK internals.

Microsoft documents [capture buffer acquisition and release](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
and the [event-driven capture sequence](https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream).
Holding a packet beyond the processing period can risk loss. Elapsed lease
time includes descheduling, SDK and callback time; it is not measured DSP CPU
cost. Optional instrumentation can itself perturb timing.

## Exact bounded native experiment

Source `097bbf9d759bcbf76a86902ccdb6ab613447ecd3` pins all 346 native build inputs.
MSVC Release compiled both diagnostic targets successfully. The fixture is
615,936 bytes, SHA-256
`4f6d228e927e29a5882c3d52859a05d2d08d7b94119d9ba5102e38fb13f5b876`.
The queue unit executable is 22,016 bytes, SHA-256
`230a74ec42ed07470cec5af34477eff259853cbab3d559c2be67ee54eda10efe`.
The same binaries ran in the independent development clone, limited interactive
session 1. An initial multiple-render-endpoint guard refused before any audio;
the next run explicitly pinned the previously tested virtual HDA endpoint.
Endpoint defaults and other applications' volume/routing were not changed.

The owned ephemeral source was unmuted at session volume 1.0, supplying a
deterministic 48 kHz stereo bank. Loopback captured one selected channel,
requesting ten seconds per attempt. Prepared SDK metadata reported 4,800 buffer
frames, a 10 ms device period, 10,000,000 Hz QPC and zero stream latency. The
reported latency is not evidence of zero physical latency. All traces fit the
queue with zero drops and intact sequence order.

| Attempt | Actual fixture exit | Saved raw frames | SDK gap | Longest held lease | Longest event wait |
| --- | ---: | ---: | --- | ---: | ---: |
| 1 | 0 | 480,000 | None | 5.6751 ms | 12.9985 ms |
| 2 | 2 | 215,040 | 480 frames at sequence 448 | 13.2841 ms | 15.6761 ms |
| 3 | 2 | 90,240 | 480 frames at sequence 188, then 1,440 | 0.2972 ms | 64.9192 ms |

Independent regeneration matches every preserved raw sample exactly, including
the aligned 64 leading zeros. The first workflow saves/reopens and exports its
in-process −6 dB EQ; independent export error is zero. The other two retain
their first discontinuity fault and partial raw media and are not completed
recordings. Their actual exits remain failures. Diagnostic runner exit zero
means collection completed, not that all workflows passed.

For attempt 2, sequence 446 holds a lease for 13.2841 ms; sequence 447 takes
89.3 microseconds, followed by the flagged packet at position 216,000 instead
of 215,520. For attempt 3, a roughly 65 ms wait occurs after release, followed
by the flagged packet at 91,200 instead of 90,720. The callback receives these
flags and positions from Windows before processing that packet. This narrows
the fault location, but does not isolate QEMU, driver, OS scheduling or an
algorithm as its cause. The existing strict discontinuity refusal remains.

## Interrupted comparison and cleanup limits

A separately prepared cable-endpoint comparison never reached audio capture.
The runner's fixed-session assertion rejected session 2 after launching only
the queue unit child. Its exit was not collected and remains **unknown**;
stdout does not substitute for an observed process exit. The task was then
observed terminal/Ready with result 1. No cable reliability claim follows.

The desktop subsequently showed equalizer activity, so further mutations/tests
were stopped to preserve that work. Read-only binary collection and unregister
of the exact owned refused task both lost SSH access (exit 255); unregister
was not confirmed. The clone was subsequently observed shut off and was not
restarted. On next authorized exclusive use, first remove only
`SoundCurrent-Owned-Capture-Trace-20261008-v3` after checking it is terminal.
Original Copperfin VMs and the clean template remain preserved.

## Evidence and next implementation task

The [receipt](../tests/results/X007/2026-10-08-windows-capture-trace.json) and
[56-payload capsule](../tests/results/X007/2026-10-08-windows-capture-trace.zip)
retain source hashes, build/test logs, native results, original audio, traces,
faults and independent analyses. No executable, DLL, credential or private
runner is included. Hosted verification checks exact Git inputs, archive hashes,
actual exits, unknown/refused states, raw fidelity and EQ output without audio
replay. Seventeen mutation controls refuse false qualification, replaced failed
exits, an invented successful unknown exit, trace loss/order corruption, altered
audio despite a matching replacement hash, and fabricated fault positions.

**Next implementation task:** admit bounded native packet-copy storage before
Start, copy the acquired SDK data and release its lease before invoking the
processing callback. Preserve raw positions/timestamps/flags and release errors,
prove no pointer survives release, audit resource limits and callback behavior,
then compare same-endpoint traces with all original failures retained. This
does not by itself solve long event waits or guarantee continuity. Improve the
private harness to verify the actual limited owner/parent-child session instead
of assuming session number 1, and use the separate installer clone for further
qualification when development VM ownership is uncertain.

Production-EQ active Stop still needs its independent lease investigation.
Installed capture, physical/sustained audio, monitoring/duplex, localization and
all full frozen-reference parity gates remain open. The full goal is active.
