# Native desktop punch checkpoint

Date: 2026-10-06. P004 / M2, short owned Linux workflow only.

The opt-in `sc-pipewire-punch-ui-fixture` uses the production Qt window, canonical
project controller, duplex recording owner, readers/writers and existing PipeWire
daemon. It opens a project, edits punch locators with the visible dialog, checks
dirty state and Undo/Redo, explicitly saves/reopens, prepares 32 armed mono tracks,
selects all owned input/master ports, and starts/stops with desktop controls.

Playback starts at 137 and runs 240000 frames. Punch is `[48150,144164)`. The GUI's
current declared input latency is zero; per-lane nonzero simulated-delay evidence
belongs to the separate [engine fixture](55-native-punch-qualification.md).
Monitoring alternates Off/Post-EQ. Live gain changes on the first/last arms and
Undo use acknowledged applied-frame receipts. Raw takes are independent of EQ;
full stereo output includes the existing file, live inputs and signed matrix.

An optional clock observer is forwarded by the recording worker into existing
native audit context. Defaults remain null. The audio processing implementation
is unchanged. Fixed admitted callback storage retains native clocks and complete
elapsed/CPU/resource observations. Control-side checks independently derive the
raw origin's within-cycle offset and replay the full EQ/matrix output oracle.

## Evidence

- Debug and ASan/UBSan/LSan affected controller/timeline groups pass 2/2 each.
  These are targeted checks, not a new full-suite sanitizer result.
- The first 32-track native GUI run passes sample/state/timing gates, but its
  screenshot shows only the top of the scrollable window.
- A subsequent explicit scroll/reachability assertion fails before Record/native
  activation. The failed source, executable, project and logs are retained. Its
  original geometry was not recorded, so the precise original cause is unknown.
- The fixture now waits for the selected two-band inspector to render and bounds
  repeated layout/scroll settlement to two seconds. It reports geometry on future
  failures. No production UI change was made based on that observation.
- The final 32-track native run passes: 3,072,448 raw samples and 480,000 stereo
  output samples exactly match; peak 3.6321504 retains float headroom. The visible
  prepared screenshot includes punch controls and the prepared playback end.
- All 32 journals contain 96014 frames at timeline 48150, exact independently
  verified native origins, and `RangeComplete`. The original underlying clip and
  media hashes survive attachment/group Undo/Redo and explicit Save/reopen.
- Final owner maximum is 3.834220ms elapsed / 3.828349ms CPU. Source/sink maxima
  are 0.331889ms / 0.038525ms elapsed. All three roles have complete timing, CPU,
  thread-resource and native-cycle coverage, passing finite gates, zero cycle
  overruns, and zero instrumented callback allocations/frees/blocking locks.
- Native nodes/links retire and default metadata stays unchanged. There were zero
  prior links; preservation of a populated external graph is not demonstrated.
- All 86 final source/executable/library pins match after terminal completion.
  All 24 borrowed inputs still match; equalizer checkouts remain unchanged.

See [machine-readable receipt](../tests/results/M2/2026-10-06-native-desktop-punch.json).
Previous 21 historical observations and this additional visibility observation
remain retained; successful later tests do not identify their original causes.
All 92 frozen contracts and F/Q/C/N axes remain unchanged and unpromoted.

## Reproduction and next task

With the required existing PipeWire version and Qt dependencies:

```sh
cmake -S . -B .cache/build-desktop-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSC_BUILD_DESKTOP=ON
cmake --build .cache/build-desktop-release --target sc-pipewire-punch-ui-fixture
QT_QPA_PLATFORM=offscreen python3 tests/verify_pipewire_punch_ui.py \
  --binary .cache/build-desktop-release/sc-pipewire-punch-ui-fixture --tracks 32 \
  --output .cache/punch-ui-result.json --failure-output .cache/punch-ui-failure.json
```

Run native fixtures serially after builds/tests finish. No hardware link is
created. This short run does not qualify sustained load, physical latency,
Windows native audio/Qt/installers, or full punch/reference parity.

Next: native punch interruption and route/disk-fault recovery; then explicit
input-latency controls, manual punch/Auto monitoring and tempo/loop/take/comping
workflows. Keep sustained native failures as independent open gates.
