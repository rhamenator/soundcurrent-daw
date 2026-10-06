# Avoid redundant smoothing work in settled EQ

Date: 2026-10-06. Scope: M2d4c6 processing headroom; long native gate remains open.

## Recorded outlier

The unchanged instrumented1800s workload from `7ffebbc` stops around382.6 audio
seconds with a1024-frame sink clock skip. All32raw lanes retain18,363,392frames,
no rejected/missing file frames; sink retains18,361,344. Read-only verification
checks all587,628,544full raw and36,722,688common stereo samples exactly, including
extra raw suffixes and overs (peak4.11241). All105original files, hashes, journals
and canonical state remain unchanged; no trim/attachment/Save changes the original.

Owner wall20.932047ms/CPU20.930266ms occurs at the missing cycle, ends22.457169ms
after native nsec, and fails the unchanged80%individual maximum gate (98.119%).
Complete thread-resource context records20.392ms user/0.104ms system time and zero
page faults or switches during that callback. Timeval accounting differs from the
exact thread CPU interval; it does not identify instruction/cache/frequency causes.
Owner total59,607minor faults elsewhere, maximum460per callback and no major faults,
stay visible; they cannot be attributed to this initiating callback or specific
addresses. Source/sink and disk observations remain separate; max disk queue1.
No production memory-lock, NUMA, governor, affinity or scheduler setting changed.

## Production change

PreparedEq maintains an audio-owned count of bands with a nonzero remaining ramp.
Its invariant is the number of active band ramps, bounded0..64. Constructor starts
at0. Applying an event to an idle band increments once; retargeting an active band
keeps the count. A ramp reaching its target decrements once. Stopped reset commits
all pending targets and clears both per-band remaining counts and this aggregate.

Call advance only while a band or wet ramp exists; while wet alone ramps, skip the
idle band traversal. With both counts0 the old advance body was entirely no-op.
Coefficient interpolation, first accepted sample, same-frame ingress order, double
biquad recurrence, tiny-state policy, float headroom, bypass history and numeric
fault/reset behavior are unchanged. No processor state schema/IDs/version, latency,
event limits, reserve, durability cadence or sample/deadline gate changes. No new
allocation, lock, logging, timing query or queue is added to production callbacks.
Memory admission continues to account for the prepared object with sizeof.

Preserve GPL notices and original copied-source hashes. Adaptation and potential
later reuse in the equalizer projects are recorded in reuse/studio/provenance.json;
no equalizer repository or branch is modified.

## Correctness and measured scope

A gain-only closed-form oracle exercises overlapping/retargeted/superseded ramps,
independent wet transitions, restart after settlement, stopped reset and all64
simultaneous ramps. Its mathematical float comparison is5e-7; partition results
are **bit-identical** at1/7/31/127/512frames. Existing independent center-gain,
headroom, numeric faults, exact timestamp/ingress, bypass/history, layout and
live/offline/partition tests retain their original thresholds.

A separate before/after EQ-only benchmark pins the old header/static libraries at
7ffebbc. Serial ABBA runs after all builds/readers terminate, five repeats per
process, use identical32mono/3band and1mono/64band inputs/configuration. Final-block
digests match in all cases; this is not a full benchmark audio oracle. Preparation,
reset/input generation and digest are outside the timed interval. It excludes
capture, mixing, native scheduling and parameter transitions; host competing load
is uncontrolled. An earlier comparison during builds is retained and excluded.

| Standalone case | Before medians (ms) | After medians (ms) |
| --- | --- | --- |
|32mono processors,3bands,15,400,960samples/run|342.076 /326.273|312.613 /312.491|
|1mono processor,64bands,481,280samples/run|143.721 /139.921|131.209 /133.461|

This modest isolated improvement supports removing redundant work. It does not
explain or resolve the20.9mswhole-pipeline outlier; other processing and host costs
remain. Short native/live-offline exactness and then the unchanged1800s workload
are required, retaining resource/cycle facts and all17historical observations.
Full Linux physical/load/filesystem/power-loss/unload, Windows native/Qt/install,
professional workflows, imports, equipment profiles and European localization
remain open. Full goal is active/incomplete; all92frozen contracts stay unpromoted.

## Native short verification

Twenty-second normal and four-second stall absorption each verify all 30,720,000
raw and 1,920,000 stereo samples, Save/reopen, overs and complete wall/CPU/resource
coverage within unchanged individual budgets. Normal owner maximum wall is
6.955083ms; absorption 6.523845ms. The deliberate 12-second stall retains lane17
and verifies 17,988,608 full raw/1,122,304 common output samples. Independent-copy
32-track recovery verifies every sample, new/recovered IDs, origins, alignment,
hashes and Save/reopen/previous backup, preserving originals and copied source
media/journals. These short passes do not erase historical failures or qualify
the required long native, physical or Windows workflows.
