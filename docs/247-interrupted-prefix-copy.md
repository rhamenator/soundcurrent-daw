# Verified copies of interrupted recording prefixes

Checkpoint 247 implements the shared recovery-copy primitive and native adapters.
Desktop review controls and end-user recovery workflow qualification remain next.
The full professional suite and independent native Windows/hardware qualification
remain incomplete.

## What is copied

A recording can stop with a durable checkpoint but no finalized media or published
native completion. There can also be samples and packet records beyond that
checkpoint. Complete original WAV export remains a distinct operation requiring
a finalized, wholly finite take and copying that container byte for byte.

The new prefix operation:

1. Admits the selected owned source files against a declared byte/memory policy.
2. Acquires exclusive inactive ownership and verifies the common checkpoint.
3. Pins the chosen partial/final source and reads its bounded RF64 chunk geometry.
4. Copies exactly the positive finite prefix, preserving IEEE float32 bits, signed
   zero, headroom, native rate and channel ordinals into a new RF64 WAV.
5. Hashes **all committed** sample bytes against the verified original, including
   committed nonfinite samples that are excluded from the playable copy.
6. Checks the entire pinned source before/after copying and publishes the flushed
   new file without overwriting an existing or raced destination.

Uncommitted audio, packet tails and bytes after the declared container are excluded
from the new file and remain untouched in the original. A nonfinite frame ends the
playable prefix; there is no sanitizing, gap filling or selection of later islands.
Empty playable prefixes, active/missing leases, changed/mismatched media, exceeded
quotas and pre-publication cancellation are refused. Temporary files are removed
on refusal. A directory-flush warning after publication retains the valid receipt.
OS I/O remains cooperative rather than governed by a hard syscall deadline.

The result retains the original checkpoint, sample/stamp hashes, finalization
facts and native disposition. It does not resume/finalize the original, invent
successful monitoring, infer native-to-project alignment, apply EQ, normalize,
attach an asset or replace a project file. The new WAV container differs from the
source container; its copied **sample bits** are exact.

## Shared code and native ownership

The framework-independent `copyOriginalPrefixWav` primitive is in
`sc-original-prefix-copy`. `copyNativeOriginalPrefixWav` verifies the native
pending/outcome declaration under the same raw-job lease retained through copying.
`copyPipeWireOriginalPrefixWav` retains state, receipt and raw leases in the established
order and verifies the saved project identity and paired checkpoint.

Existing inspectors now have private held-lease entry points; there is no nested
reacquisition of the same lease. Existing validation, raw/native schema meaning
and full original export stay in place. There are no new libraries or journal
migrations. This is entirely control/disk work, outside audio callbacks.

The WAVE geometry helper has an explicit header-only result: it verifies neither
samples nor a content digest. Its checkpoint policy permits bytes after the
declared container. Full WAVE content admission still requires exact containers;
prefix copying independently verifies checkpoint/sample/hash/finiteness before
publication. A fixed 64 KiB page streams audio without length-dependent buffering.

Raw/native copy defaults to a16 MiB workspace. PipeWire uses its existing64 MiB
route scope because state inspection alone admits24 MiB. The first PipeWire test
correctly refused the generic16 MiB default; its log and actual project are
retained. The repair declares the route-specific scope, rather than changing
capture pool limits or bypassing caller quotas.

## Acceptance evidence

Source `efc516a99e6b7c4b2ef6b1a5b0ccadb41367ef57`:

| Release workflow | Result | Time |
| --- | --- | --- |
| Original raw recording and prefix copies | Pass | 0.10 s |
| Native original outcomes and prefix adapter | Pass | 0.57 s |
| Full pinned WAVE admission regression | Pass | 0.01 s |
| PipeWire paired journal regression | Pass | 0.07 s |
| Final PipeWire state and prefix adapter | Pass | 0.06 s |

The first four passed in the initial selection and their dependent source remains
unchanged. The final PipeWire scope was rebuilt after the admitted-workspace fix.

Raw tests copy mono, stereo and256-channel sources with7 committed frames and7
additional uncommitted audio frames plus arbitrary trailing bytes. Whole finite
copies contain7 frames; nonfinite cuts contain3. Every copied sample byte is checked,
including negative zero and values above unity. The new WAV independently passes
full pinned content validation. Empty/zero finite prefix, existing/raced output,
missing media, changed source, cancellation, protected project paths, quota and
held inactive lease are exercised. Linux source mutations are observed and refused;
Windows pinned-write-denial behavior is encoded but remains independently unqualified.

Ten existing **actual process interruption** boundaries now also copy their verified
raw checkpoint and compare exact sample hashes. Native publication cases preserve
16-frame raw prefixes and the original Pending/Complete disposition and finality.
PipeWire proves the important unequal-checkpoint case: standalone raw journal34
frames, paired native checkpoint17, copied17 with the same exact sample hash.
All three leases remain held during the progress callback; the source state,
paired and raw journals remain unchanged and Pending remains Pending.

Fresh ASan/UBSan media build139steps passes **5/5 in21.69 s**, with leak detection
and halt_on_error: state1.22, journal0.65, native19.03, raw0.70 and WAVE0.09 seconds.
Existing guarded allocation/free/blocking-lock audits remain zero. These are real
files, leases and process-interruption tests with synthetic native outcomes; no
physical device, PipeWire daemon, VM or hardware-alignment claim is involved.

The [evidence index](../tests/results/M2/2026-10-13-interrupted-prefix-copy/index.json)
contains72 compressed text/configuration/metadata members,775 Git/checkout source
bindings and10 executable hashes. Original failure project audio and CI ZIPs remain
local; public evidence contains no PCM, executables or ZIP payload. No GUI source
edit or Linguist update is part of this checkpoint.

## Original preceding-source CI and unresolved failures

[Original PR180 run](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38345814313),
source8aec591, is terminal: Linux159/159 passes464.94 s, Windows desktop20/20
passes61.71 s and Windows cross-build passes. Windows native84/86 in341.95 s
has two failures:

- `native-monitor-startup`: completed owner cases include96 kHz and the new work/
  cycle/wait observations, followed by a generic desktop-controller wait exceeding
  the original five seconds. No controller stage/snapshot was recorded at expiry.
- `manual-recording-control`: the original aggregate60-second CTest deadline
  expires at concurrent-owner entry. Both added reserve cases already report exact
  48,128/96,000-frame results. Separate unchanged twenty-take diagnostic invocations
  pass6.72/6.72/11.13 seconds on the declared two-second profile.

Original APIs, four job logs, selected metadata and Windows ZIPs are retained.
The native ZIP **does not contain the printed failed controller Temp directory**;
its other retained owner evidence is not substituted for that failed case. Actual
failed-controller bytes and a pre-expiry controller-stage snapshot are unavailable
from the returned artifacts. Future retention must capture that owned directory.
The manual timeout likewise cannot be treated as proof that its final twenty takes
completed in that aggregate invocation.

These failures are separate from the successful copy tests and earlier PR178/176
failures. No observation deadline, clock confidence or native status is increased,
fabricated or retrospectively changed by this checkpoint.

## Next concrete work

Improve controller wait-stage diagnostics and retain its actual owned failure
project. Give the new reserve checks their own CTest workflow while preserving
the existing manual60-second and per-condition five-second gates; inspect actual
remaining failures without unchanged reruns or timeout inflation.

Connect both prefix adapters to the desktop control owner and review UI: display
committed versus copied frames, original disposition, excluded tail and explicit
copy destination; preserve result sequence, cancellation and original export.
Run real Linguist update after UI source edits and qualify actual visible controls.
Independently qualify Windows source, native playback and installers when resources
allow, then continue remaining recording and full-suite/localization milestones.
