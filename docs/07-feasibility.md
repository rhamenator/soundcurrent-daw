# Bounded feasibility evidence

Date: 2026-10-05. Scope: synthetic, silent, headless checks plus read-only audio-server inspection. **No DAW was implemented; no microphone/speaker test, VM test or reference-app test was run.**

## Environment

- C++ compiler: Ubuntu GCC15.2.0 (`15.2.0-16ubuntu1`). CMake4.2.3. Qt6Core6.10.2 present.
- PipeWire development package1.6.2; native filter header/link check described below. Official online API rendered1.6.9, so compile against the actual local headers before adopting APIs.
- libsndfile development pkg-config absent. No packages installed; no new dependency falsely reported as build-validated.
- Existing Studio SDK pinned to `65151a8fec1aa4b4e6c283e4514d7f3d9166fe2b`, built outside its checkout with desktop disabled.

## Experiment A: reuse boundary and deterministic EQ

Sources: `experiments/CMakeLists.txt`, `experiments/eq_feasibility.cpp`. Command shown in the repository README. It links the existing portable engine/WAV libraries with no Qt or device ownership in the harness. Generated signals never leave the process.

| Check | Result |
|---|---|
| Mono/multichannel finite processing |1/2/8/32/256 channels exercised with synthetic buffers |
| Sample-rate variants |44.1/48/96 kHz |
| One large block versus16/64/127/512/2048-frame chunks | Maximum sample error **0** for tested EQ profile |
| +6 dB peaking EQ at1 kHz | Maximum measured gain error **4.53215e-08 dB**, transient region excluded |
| Ordinary C++ new/new[] during prepared process region | **0** detected calls |
| Scalar post-gain immediate next process call | Passed without changing preset |
| Clip report for over-range input | Passed; confirms existing engine clamps, a DAW adaptation requirement |
| Synthetic float32 WAV write/read | **48000 frames**, reopened samples exactly equal |
| Existing SDK CTest suite | **3/3 passed**: dsp-response, studio-engine, studio-wave |

The global allocation hooks observe ordinary C++ new/new[], not aligned allocators, malloc, OS page faults, frees, hidden driver activity or mutexes. This is useful partial evidence alongside source inspection, not a complete RT proof. No simultaneous control mutation was attempted; the existing configure/process nonconcurrency rule was respected. No CPU/deadline benchmark or quality listening was performed. EQ-only partition equality does not prove delay/reverb/modulation/host equivalence.

WAV generation is a synthetic feasibility test of reused utilities, not a track recording workflow, project save/reopen, capture queue or libsndfile qualification. Those are SLICE-001 tasks.

The additional commands used after the README's CMake configuration were:

```sh
cmake --build .cache/eq-probe --target soundcurrent-engine-test \
  soundcurrent-dsp-test soundcurrent-studio-render -j 4
ctest --test-dir .cache/eq-probe/sdk --output-on-failure
cmake --build .cache/eq-probe --target pipewire-api-feasibility -j 4
.cache/eq-probe/pipewire-api-feasibility
```

## Experiment B: installed PipeWire API and read-only server

`experiments/pipewire_api.cpp` only checks headers/linkage/version, `PW_FILTER_FLAG_RT_PROCESS`, filter-event structure availability and native64-bit atomic lock freedom. It passed against library1.6.2: RT flag4, event structure80 bytes,64-bit atomics lock-free. It does not create or connect a node. `pw-dump` read-only enumeration succeeded:83 objects and9 nodes at observation time. These counts are transient and not product requirements. No device names or IDs are copied into tracked source. [Structured results](../research/feasibility-results.json) preserve these observations.

This establishes a locally usable dev API and reachable server. It does not establish synchronized capture/playback ports, actual RT thread context, callback timing, quantum/rate negotiation or physical recording alignment. The first adapter experiment must use owned virtual nodes and must not redirect the user's playing music.

## Work stopped at this boundary

Completed: source inspection, exact baseline verification, requirements/architecture/dependency/roadmap planning, portable DSP compile and synthetic checks. Deferred deliberately under the user's scope: a real application, timeline/editor, device capture writer, saved project implementation, plugin host, new effects/instruments and publication.

Concrete next task: **SLICE-001/S1+S2 — Qt-free one-track session with stable object/parameter IDs and validated atomic save/reopen**, using synthetic media fixtures. Then S3/S4 add the float-headroom EQ adapter and bounded capture-to-disk worker before S5 introduces native ports.
