# Owned native punch qualification

Date: 2026-10-06. P004 / M2. Short Linux native engine workflow; full punch and
professional-suite parity remain incomplete.

## Fixture and independent coordinates

`sc-pipewire-punch-fixture` is an explicit integration target, using the production
PipeWire duplex owner and existing daemon. It creates only owned source, recorder
and stereo sink nodes. No hardware, default endpoint, rate, quantum, governor or
scheduler policy is selected or changed. Its `synthetic` mode exercises the same
sample oracle, graph readers, recording workers, journals and Save/reopen without
creating native nodes. No production processing or equalizer source was changed.

The project starts playback at frame 137. Desired punch is `[48150,144164)`, with
32 mono Post-EQ arms. Declared delays repeat 4097,0,41,200 frames; the first binding
therefore has the largest delay. A bijective input-channel permutation and
stateless device-coordinate signal provide distinct reproducible raw inputs.
Each source channel simulates its declared delay independently of capture and
attachment. Playback extends to 148261, retaining preroll and 4097-sample postroll.

The first captured origin is unsuitable for anchoring a full playback oracle:
it omits preroll and differs between lanes. Instead a fixture-only callback audit
retains the first native cycle that advances the graph, publishing its immutable
clock with release/acquire synchronization. It also retains each lane's native
cycle and sample offset at punch-in in fixed storage. The sink begins at the
playback anchor and checks every full-range file/live matrix output sample.
Control-side verification checks each raw sample against the desired timeline,
not against the lane's recorded waveform; its expected origin comes from the
independently retained actual native clock and within-cycle offset.

Callbacks use fixed admitted storage. All reading, hashing, media inspection,
JSON, sorting, attachment, undo/redo and project persistence occur after native
join. Existing allocation/free/lock instrumentation and complete elapsed/CPU/
thread-resource timing include the fixture observations. This does not measure
instrumentation overhead separately or establish hard real-time guarantees.

## Qualified workflow

Both optimized and ASan/UBSan/LSan synthetic runs pass. The optimized native owned
run then passes serially after all owned builds/tests are terminal:

- Exactly 3,072,448 raw samples and 296,248 stereo output samples checked; zero
  difference, no missing frames and output peak 3.6612954 preserves float headroom.
- Each lane contains 96014 frames, uses its separate declared-latency raw window,
  retains the exact first-sample origin and final `RangeComplete` journal, and
  attaches at frame 48150 with source offset 0.
- Observed native quantum 1024; first four punch-in offsets 910,909,950,85 exercise
  separate nonaligned boundaries and distinct cycles. Aggregate origin matches
  the zero-delay second binding rather than the delayed first binding.
- Full preroll/punch/postroll file/live monitoring matches an independent signed
  stereo matrix. Flat EQ is deliberate in this exact raw/matrix workflow;
  non-flat quality remains covered separately, not inferred from this run.
- Original canonical project/media remain unchanged until explicit Save. Grouped
  attachment undo/redo and exact schema1.4 Save/reopen pass.
- Owner maximum 2.031224ms elapsed/2.027194ms CPU; source maximum 0.452567ms elapsed
  and sink maximum 0.041642ms. All three roles have complete elapsed/CPU/resource
  and native-cycle coverage, finite timing gates pass and cycle overruns are 0.
  Instrumented callback allocation/free/blocking-lock counts are 0.
- Default metadata stays unchanged and owned nodes/links retire. No prior links
  were present, so populated pre-existing-link preservation is not exercised.

The supervisor retains the project, stdout, stderr, route observations and
scheduler observations on both success and failure. A specific child is waited
to terminal state before cleanup or subsequent work. Eleven altered receipts
are rejected, including missing samples, offset/latency errors, incomplete
elapsed/CPU/resource/cycle coverage, deadline misses and false physical claims.
The final stricter cycle-coverage gate is replayed on the actual native result;
its original supervisor bytes and actual executable are retained with hashes.
See [evidence](../tests/results/M2/2026-10-06-native-punch.json).

## Remaining qualification

Simulated input delay is not measured backend, converter or physical roundtrip
latency. This is an engine-level native workflow, with desktop state/control
workflow qualified separately through synthetic endpoints. Full native desktop
Prepare/Record/Stop/attachment still needs integrated evidence. Native interruption
and route/disk faults during punch, manual punch, Auto monitoring, beat/tempo
locators, record-stop/continue, loop/take lanes/comping remain required.

This short flat-EQ native fixture does not close sustained performance, full
non-flat workload, physical device, Windows native/Qt/installers or any frozen
F/Q/C/N axis. All 21historical observations and 92frozencontracts remain retained
and unpromoted. No dependency, equalizer write, signing purchase, VM or publication.
