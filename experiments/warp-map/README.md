# Original exact warp-map experiment

GPL-3.0-only C++20 control-side planner and original bounded probes. See
[checkpoint146](../../docs/146-normalized-warp-map-planner.md) and
[ADR105](../../docs/decisions/105-normalized-warp-map-planner.md).

This is outside the shipping application workflow. The root CMake flag
`SC_BUILD_WARP_MAP_PROBE` defaults OFF; enabling it builds the geometry CLI and
an independent standard-library Fraction oracle, with no install rule. A minimal
fresh Linux build avoids Qt, native audio, media codecs and vendor recompilation:

```sh
cmake -S . -B .cache/build-warp-map-new -G Ninja -DCMAKE_BUILD_TYPE=Release -DSC_BUILD_WARP_MAP_PROBE=ON -DSC_BUILD_MEDIA=OFF -DSC_BUILD_RESAMPLER=OFF -DSC_BUILD_PIPEWIRE=OFF -DSC_BUILD_DESKTOP=OFF
ionice -c3 nice -n19 cmake --build .cache/build-warp-map-new --parallel 2 --target sc-warp-map-probe
ctest --test-dir .cache/build-warp-map-new -R '^experimental-warp-map-geometry$' --output-on-failure --no-tests=error -V
```

Each CTest run owns a new UUID evidence child. Direct `geometry_oracle.py`
execution requires an exclusive output root; `--new-run-under` enables repeated
CTest runs without overwriting observations. The probe's16 KiB JSON-line/4096
request bank is a harness limit. Trusted planner admission is separate.

The renderer is compiled independently against explicitly known local
`libsc-session.a` and pinned `libsc-rubberband.a`; exact original commands, compiler,
library/executable hashes and source snapshots are retained in the capture.
Do not reuse an unknown ABI or a damaged build cache. `run_candidates.py` needs
NumPy (actual2.3.5), restricts BLAS threads and limits Linux address space to512 MiB.
It runs52 terminal owned cases under60 seconds total /10 seconds each. It never
opens an audio endpoint. The geometry oracle does not need NumPy.

`analyze_candidates.py` performs post-run onset/phase diagnostics. It explicitly
flags censored windows and does not qualify listening, perceptual quality,
arbitrary groups or Q-STRETCH. `retain.py` is the original-run recipe including
its original retention failure; complete WAV bytes are addressed by SHA256 and
shared only when identical. It never copies executables. Committed evidence
inspection does not rerun a processor or native audio.
