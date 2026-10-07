# Canonical monitoring feedback and bounded repeated recording

Scoped recording checkpoint,2026-10-07 UTC. Full DAW parity remains incomplete.
See the [receipt](../tests/results/M2/2026-10-07-recording-monitor-feedback.json).

## Actual widget defect and fix

Recording preparation captures an accepted command prefix through a barrier. The
monitoring dropdown can still be enabled at the last GUI refresh when Prepare is
requested. A selection made before the next GUI refresh is rejected by that
barrier. Previously the widget retained the rejected value, even though the saved
model and prepared endpoint retained the original mode. Checking only the cached
last-rendered mode could leave this misleading value displayed indefinitely.

The actual Qt widget regression reproduced that mismatch with the unchanged UI
implementation. Its source, executable/library hashes and output were retained
before the fix. This is a synthetic UI development failure, not a new native audio
failure number.

The handler now restores canonical mode immediately on rejection with signals
blocked. Polling also compares the actual dropdown value to canonical state.
These changes do not submit a replacement edit, create history, reprepare or
activate audio. The accepted-prefix preparation behavior is unchanged.

Tests cover Off→Post-EQ, Post-EQ→Auto and Auto→Off rejected input during preparation,
immediate correction and eventual Ready feedback. Each verifies unchanged model
revision, clean state and prepared intent, with no job or activation. After Stop,
an accepted selection, Undo, Redo and Save follow canonical state without extra
endpoint construction or activation. Existing accepted-change/immediate-Prepare
and prepared-graph Undo tests remain required. These fake-endpoint UI tests do
not qualify native single-track Auto monitoring or native Windows operation.

Local Debug38/38 desktop/synthetic tests pass in44.81s, including475 actual Qt UI
checks. No engine/backend/recording persistence source is changed by this fix;
the only production source change is `ui/studio_window.cpp`.

## Bounded repeated-take timing experiment

An opt-in `sc-pipewire-manual-stage-fixture` uses the existing unchanged manual
recording path and original full native oracle. Test-only linker wrappers retain
the four worst complete bridge intervals and inclusive CapturePipe / EQ / mix
costs, using fixed storage. They add clock reads and measurement cost within the
original callback timing gate. Production libraries gain no profiling hooks or
clock reads. Joined observations are written only after all callbacks terminate,
before later model/result verification can fail.

One actual owned-route run completed three replenished take windows on32 tracks,
mixed Off/Post-EQ/Auto monitoring and declared0/41/200/4097-frame input delays:

| Gate | Original native result |
|---|---:|
| Rate /quantum /target |48kHz /256 /480,000 frames|
| Raw samples, all96 original lane files |1,557,696, exact|
| Stereo output samples |960,000, exact|
| Float output peak |3.186612844467163|
| Save/reopen /grouped Undo/Redo |pass /pass|
| Owner callbacks /complete bridge observations |1,875 /1,875|
| Owner maximum wall /CPU ns |1,159,207 /1,154,469|
| Same-clock bridge wall /CPU ns |1,124,448 /1,119,515|
| Same-clock inclusive mix wall /CPU ns |1,044,558 /1,040,460|
| Same-clock inclusive EQ wall /CPU ns |992,058 /855,743|
| Same-clock raw capture calls |0|
| Callback allocation /free /blocking locks |0 /0 /0|
| Original finite deadline /current-cycle gates |pass /pass|

The selected owner maximum and worst bridge share the exact clock/cycle/position,
rate and quantum. Mix, including EQ, dominates that measured callback. The EQ
stage is nested within mix; their costs must not be added as disjoint work. The
measurement overhead is retained, not estimated away.

The32-track recording, nonflat output, exact latency/origin, original media,
replenishment, grouped edits and Save/reopen gates are exercised together in this
bounded run. Default-device metadata and pre-existing links remain unchanged;
only owned routes are created and cleaned up. This is not a sustained workload,
physical interface latency qualification, native Windows claim or increased
project-track ceiling.

Original48's CPU/cycle failure was **not reproduced**. It remains retained and
unresolved; a passing later run does not establish its cause or clear earlier
failures. Runtime failures remain48. No compensating latency, weaker budget or
audio replay of the original failure was used.

## Reproduction and next implementation

`verify_repeated_manual_stages.py` joins the retained stage data to the original
full native acceptance receipt. It requires complete observation coverage and an
exact owner-maximum clock match. The companion mutation test refuses22 altered
clock, count, timing, sample extent, save/Undo and route cases. Both accept an
explicit relocated `--project` and preserve original receipt bytes. The archived
payload is pinned independently of its internal manifest; CI verifies relocation
without starting an audio daemon or replaying audio.

The existing manual-stage synthetic test verifies wrapped/unwrapped sample,
origin, command and state equivalence with zero RT allocation/free/locks. Live
experiments run after all owned local builders/tests terminate. This experiment
adds no production dependency or default-device mutation.

Next implementation: add the serialized Qt manual-recording control owner and
connect Play, Punch In/Out, next take, Stop/Cancel and retained take-group adoption
to canonical state. Preserve the accepted command-prefix and UI reconciliation
contracts; qualify late Cancel through finalization and monitoring Undo. Continue
performance work when meaningful stage evidence of an actual outlier is available.
Do not hold unrelated required features behind repeated passing short runs.

All92 frozen parity contracts and X004/X005/X006/Europe/independent Windows remain
required. The256-track implementation ceiling remains an X006 gap. Equalizer
working trees are unchanged and their24 reviewed inputs remain identical.
