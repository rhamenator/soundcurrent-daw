# SLICE-001 evidence

[2026-10-05 state foundation](2026-10-05-state-foundation.json): S1/S2 subset only. Native Linux Debug build: 54 assertion checks, 1/1 CTest passed. AddressSanitizer/UndefinedBehaviorSanitizer: 1/1 CTest passed. Developer CLI created/reopened a Unicode-path project and rejected creation over an existing project. Windows x86_64 core, tool and tests cross-compiled; native runtime not executed.

Reproduction is in the root README. Sanitizer gate:

```sh
cmake -S . -B .cache/build-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build .cache/build-sanitized
ctest --test-dir .cache/build-sanitized --output-on-failure
```

The state fixture is three arbitrary bytes with a known SHA-256, not an audio recording. Tests exercise snapshots and semantic parameters; they do not establish playback, in-process EQ response, capture, exports, full interrupted-recording recovery or DAW parity. No user audio device was opened or rerouted. Full slice gates remain in `docs/05-first-slice.md`.
