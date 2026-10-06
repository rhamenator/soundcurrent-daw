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

## Prepared EQ and transport

[S3 evidence](2026-10-05-prepared-eq.json): Linux state + engine tests and sanitizer gates; Windows core/tests cross-build only. Measured peaking response, float overs, exact block partition/live-offline agreement, sample-timed smoothing/bypass history, bounded overload/failure, concurrent SPSC wrap and retirement credits. These are host-owned processor/transport tests, not native recording or full graph qualification. See the exact [contract](../../../docs/11-engine-contract.md).

## RF64 API feasibility

[RF64 probe](2026-10-05-rf64-probe.json): Linux installed libsndfile1.2.2, active-writer prefix header/flush/reopen and final float-overs retention. Independent RF64 chunk/GUID/sample reader passes. No capture thread, recording journal, process-kill/power-loss or >4 GiB file is exercised.

```sh
cmake -S experiments/media -B .cache/media-probe -DCMAKE_BUILD_TYPE=Debug
cmake --build .cache/media-probe
.cache/media-probe/sndfile-feasibility .cache/new-capture-probe.wav
python3 experiments/media/verify_rf64.py .cache/new-capture-probe.wav
```

Supply a new path; the probe exclusively creates it and refuses overwrite. It writes synthetic samples only and opens no audio device. The format-signature/known-sample verifier is deliberately limited to this fixture.

## Headless capture and recording recovery

[S4 evidence](2026-10-05-recording.json): Linux Debug and ASan/UBSan gates, fixed pool transport, concurrent production disk supervisor, exact ten-second raw RF64 take, project attach/save/reopen/relocation, persistent queue-gap/invalid-input diagnostics, cancellation, verified-prefix copy recovery, injected publication interruptions, SIGKILL and kernel RLIMIT_FSIZE short-write/EFBIG. Windows media/tools/tests cross-link against a locally built pinned libsndfile1.2.2 DLL. Native Windows execution remains unverified.

[CLI/independent-reader evidence](2026-10-05-recording-cli.json) is reproduced by `python3 tests/verify_record_cli.py` after the Linux core build. This generates/inspects an owned synthetic project on a Unicode path, rejects overwrite, checks RF64 chunks/GUID/frame counts and independently compares all480000 float samples. Fixtures clean up their owned files.

The large capture assertion count includes per-sample equality at multiple channel layouts/quantums, not millions of distinct parity workflows. No PipeWire/device/default route is exercised. Physical ENOSPC, actual power loss, >4 GiB RF64, native deadlines, and Windows runtime/filesystem qualification remain open. See [recording contract](../../../docs/13-recording-contract.md).

## Immediate control ingress

[S6b evidence](2026-10-05-immediate-controls.json): Linux Debug and ASan/UBSan seven-group CTest gates, Windows cross-build only,128-command published-prefix snapshots, mixed scheduled/manual ordering, full combined budget, pressure/fault handling and20000 concurrent edits replayed exactly at applied receipt frames. Shared recording and disk-playback forwarding is exercised. The opt-in native file-player fixture now changes gain from its control thread and compares captured output against receipt-driven offline replay.

```sh
ctest --test-dir .cache/build-core --output-on-failure
python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-core/sc-pipewire-playback-fixture
ASAN_OPTIONS=detect_leaks=1 PIPEWIRE_DLCLOSE=false \
  python3 tests/verify_pipewire_fixture.py \
  --binary .cache/build-sanitized/sc-pipewire-playback-fixture
```

Native harnesses run serially. The sanitizer native command retains PipeWire modules solely for the existing unload-memory diagnostic. An initial native sanitizer attempt stopped on a gap; its failed evidence is retained, and later passes do not resolve its cause or qualify load/clock transitions. No Qt GUI, physical speaker latency or Windows runtime is exercised. See [the contract](../../../docs/16-immediate-controls.md).

## Asynchronous desktop project editing

[S6c evidence](2026-10-05-desktop-controller.json): Qt6.10.2 project-editor preview, separate canonical-model and project-I/O workers, immutable snapshots, save revision/content correctness, scalar gesture undo/cancel, bounded command pressure, priority shutdown and a close barrier before dirty choices. Linux nine-group desktop/core CTest and sanitizer suites pass; final modified desktop fixtures are separately requalified. UI fixtures use the offscreen plugin, keyboard/wheel/slider controls, locale decimal input, same-ID reordered band reload and actual Save/Discard/Cancel dialogs. The32-band view fits1000×640 and lower controls remain reachable by scroll.

No recording/playback/export or native audio is connected to this GUI yet. Headless Windows regression builds pass; the new Qt window/controller has not been compiled or run on Windows. Language/native accessibility/runtime deployment remain open. The owner-added equipment-profile/editor feature X005 is required and unimplemented here. Reproduction and limits are in [the desktop contract](../../../docs/18-desktop-controller.md).

## S6d native playback ownership

[Evidence](2026-10-05-native-playback-owner.json) records10 Linux CTest groups with sanitizers, the shared Windows cross-build, simulated native clock/layout/failure tests and production-owner PipeWire sample/disconnect tests. It preserves an unexplained native timeout and distinguishes serial success from unresolved deadline/load/shutdown and normal dependency-unload qualification. The GUI remains a project editor; asynchronous audio binding is next.

## S6e desktop playback

[Evidence](2026-10-05-desktop-playback.json) records11 Linux debug/sanitizer test groups, explicitly injected controller/UI fixtures, and real owned PipeWire through the actual window/worker. Native gain/undo is replayed at exact receipts and compared with an independent captured sink; graph/default preservation and asynchronous close are checked. Earlier native timeout/module-unload and hardware/Windows GUI gates remain open. Next is recording ownership/UI, then export.
