# Anchored alternative scheduler experiment

Original GPL-3.0-only control-side C++20 scheduler, fixed contract and bounded
Python process bank. This is not in the shipping CMake/editor/install workflow.
[Checkpoint147](../../docs/147-anchored-stretch-candidate.md) and
[ADR106](../../docs/decisions/106-transient-protected-warp-next.md) record results
and the concrete next implementation task.

The exact candidate sources and upstream MIT notices are retained in the evidence
archive: Stretch a670068d9aeb64913331d5cc29337b19a457a7df and Linear
 de55e6a50ffcf6f8f43f649692d94691c7025151. Do not use current upstream main,
automatic FetchContent or optional unreviewed FFT/SIMD backends. The original
command, compiler/library/executable hashes, regular include wrapper files and
source snapshots are retained in build-inputs.json / candidate-source.

The actual source review was staged under `.cache/warp-map-plan/alternative-review`.
With those exact sources and the independently qualified session support library:

```sh
ionice -c3 nice -n19 c++ -std=c++20 -O2 -DNDEBUG -fno-fast-math -Wall -Wextra -Wpedantic -Iinclude -Ithird_party -I.cache/warp-map-plan/alternative-review/signalsmith-stretch -I.cache/warp-map-plan/alternative-review/linear/include experiments/anchored-stretch/render_probe.cpp experiments/warp-map/warp_map.cpp .cache/build-warp-map-geometry/libsc-session.a -pthread -o .cache/warp-map-plan/sc-anchored-stretch-probe
ionice -c3 nice -n19 python3 experiments/anchored-stretch/run_bank.py .cache/warp-map-plan/sc-anchored-stretch-probe .cache/warp-map-plan/NEW-EXCLUSIVE-BANK
ionice -c3 nice -n19 python3 experiments/anchored-stretch/analyze_bank.py .cache/warp-map-plan/NEW-EXCLUSIVE-BANK
```

Do not substitute an unknown session ABI or overwrite evidence. The analyzer
requires NumPy; actual2.3.5. Geometry/retained inspection is standard-library only.
Seed20261010, one-thread processes,512MiB child address ceiling,10s child/60s bank
limit and complete generated pre-roll/trimmed output are explicit. The actual
bank24 terminal exits0 took2.708s. Impulse-only timing success does not qualify
attack/group/phase/listening workflows; this adapter is not selected for shipping.
