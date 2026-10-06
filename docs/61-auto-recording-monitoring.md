# Auto monitoring during recording

Date: 2026-10-06. P004 / M2 recording monitoring; bounded workflow checkpoint.

## User workflow

Choose **Auto — monitor during recording** for a selected track in the Recording
inspector. Preparation captures the accepted saved preference for every armed
track. Routes still require explicit selection. Stop and prepare again to change
a prepared owner. Save/reopen restores the preference passively, without starting
or arming audio. Undo/Redo uses stable track identity and the existing bounded
history. The combo ignores an unfocused mouse wheel and identifies Auto in the
armed-track and prepared-mode summaries. New text is contextual Qt translation
input; no additional delivered language is claimed.

During prepared musical punch `[B,E)`, Auto selects live input only at those
project samples. Existing clips resume at E; a track with no clip supplies file
silence outside the window. Ordinary continuous recording monitors throughout
its recording run. Off retains file playback; Post-EQ selects live input throughout
the duplex run, including preroll and postroll. Both live modes pass through the
same track EQ and mix matrix. None of these modes prints EQ into raw takes.

The declared input delay L is independent: capture remains `[B+L,E+L)` and the
result attaches at B. A late raw capture window must not delay the Auto switch.
Input monitoring represents the samples arriving now; it does not time-shift a
physical microphone to remove its hardware delay.

## Engine and persistence

Schema 1.6 adds the stable mode identifier `auto-recording`; it introduces no new
JSON keys. The reader retains explicit 1.0–1.5 migration and older Off/Post-EQ
semantics. Older schemas reject the new identifier, and unknown strings/invalid
enums are rejected. Old projects retain their original Off default.

Preparation resolves each monitored lane to an immutable ordinal and half-open
selection window. `MixPlayback` validates the entire replacement batch before
consuming file data. All file lanes still advance and report reader faults during
live selection. All live planes are copied before aliased native outputs are
cleared. Selection is bounded to the current block and precedes the continuous
EQ call, so punch boundaries do not reset filter state or parameter smoothing.
No new callback allocation, deallocation, blocking lock, logging or disk I/O is
introduced. [ADR 047](decisions/047-recording-only-auto-monitoring.md).

## Reference relationship

[Steinberg's Cubase Pro 15 VST preferences](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/preferences/preferences_vst_r.html)
distinguish input monitoring controlled manually, by record enable, during
recording, and in stopped/recording tape-style operation. This checkpoint targets
the recording-only behavior. It does not complete the other modes or full P004.
The topic is labeled 15.0; it does not independently qualify patch 15.0.30.

[Bitwig's Recording Clips guide](https://www.bitwig.com/userguide/latest/recording_clips/)
describes Off/Auto/On and Auto as the default. The retrieved topic does not define
every armed/stopped/playback/punch transition. Complete Bitwig Auto behavior and
patch-specific comparison remain uncertain pending qualified reference testing.
The frozen baseline and separate functional/quality/content/native-project axes
remain unchanged.

## Acceptance and remaining gates

The tests cover strict persistence/older-schema rejection, grouped mode and take
Undo/Redo, passive Qt restore, required output routes and input-loss finalization.
Thirteen mixed Off/Post-EQ/Auto partition workflows exercise mono/stereo channel
permutation, aliased output, independent delays, underlying clips, one-frame and
partial-block boundaries, 1/7/127/256/mixed quanta, and a continuous nonflat EQ
oracle. Auto interruption cases retain exact durable prefixes before, during and
after the monitor window. Near-int64-limit tests and invalid/empty replacement
intervals verify checked timing and preflight without cursor consumption.

[Dated receipt](../tests/results/M2/2026-10-06-auto-recording-monitoring.json)
records exact source/executable/library pins, all build/test logs and native output.
All 29 Linux Debug groups pass (27.14s); those functional tests overlapped separate
compilation, without a performance claim. All 29 ASan/UBSan/LSan groups pass
(73.13s). Windows core cross-compilation passes; no native Windows claim follows.

After all owned CPU producers were terminal, the short isolated 32-track native
desktop run passed with eight Auto, eight Post-EQ and sixteen Off arms. All
3,072,448 raw samples and 480,000 stereo output samples matched exactly, including
live EQ receipt replay over the selected input/file stream. Peak 3.506171 retains
float headroom. Callback allocations/frees/blocking locks were zero; complete
wall/CPU/resource/cycle gates passed. Owner maximum wall time was 4.019466ms and
CPU time 4.018049ms at the observed 512-frame/48kHz quantum. The two pre-existing
links and default metadata remained unchanged, and all owned resources retired.
Twelve altered receipts were refused; the prior declared-delay receipt still
passes the extended verifier. All 152 launch pins matched after termination.

The [prepared UI screenshot](../tests/results/M2/2026-10-06-auto-monitor-prepared.png)
was visually checked: Auto, mixed armed modes and 4097 samples / 85.354ms are
visible. The fixture's exact run-frame API overrides the seconds widget's existing
600-second default; prepared playback end 240137 and actual 240000 rendered frames
are verified independently. No new failure observation occurred. All prior 25
observations and original unknown terms remain preserved; this short pass does
not resolve sustained failures.

Reproduce only after all owned build/test producers are terminal:

```sh
cmake --build .cache/build-desktop-release --target sc-pipewire-punch-ui-fixture
python3 tests/verify_pipewire_punch_ui.py \
  --binary .cache/build-desktop-release/sc-pipewire-punch-ui-fixture \
  --tracks 32 --input-latency --auto-recording \
  --output .cache/auto-monitor-native.json \
  --failure-output .cache/auto-monitor-native-failure.json
```
No full frozen contract is promoted. Manual punch while playback continues,
monitoring while armed or stopped, tape behavior, loop recording/take lanes,
comping, physical timing, sustained deadlines, native Windows and full European
translation/review/UI qualification remain required.

Next implementation task: admit sample-boundary manual punch commands during an
already running playback generation, with prepared capture resources, bounded
commands/acknowledgements, off-audio writer handoff, ordered Stop/fault recovery
and grouped take admission. Repeated punches must retain playback and EQ history.
