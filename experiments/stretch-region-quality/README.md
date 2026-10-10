# Explicit processing-region quality diagnostic

Original GPL-3.0-only C++20/Python diagnostic. It is outside the shipping CMake
build, never opens audio endpoints and has no native Windows qualification.
[Checkpoint145](../../docs/145-stretch-region-quality.md) records actual evidence.

`build.py` links an explicit already-qualified Linux build cache only after
checking287 production source hashes against the PR85 capture. It records compiler,
library/source/audit hashes and command/outcome; it cannot qualify a changed ABI.
It uses the actual `SNDFILE_LIBRARY` path from that cache's CMake configuration.
Use a fresh complete build when sources or compiler/library ABI differ.

Example for the retained local cache, under low disk/CPU priority:

```sh
ionice -c3 nice -n19 python3 experiments/stretch-region-quality/build.py .cache/build-stretch-region .cache/region-quality-new
ionice -c3 nice -n19 python3 experiments/stretch-region-quality/run.py .cache/build-stretch-region/sc-stretch-render-worker .cache/region-quality-new/sc-region-quality-probe .cache/region-quality-new/run-2
python3 experiments/stretch-region-quality/analyze.py .cache/region-quality-new/run-2
```

Confirm the helper name/path from the build before running. `run.py` requires
NumPy (actual run2.3.5), restricts its BLAS threads and applies a512 MiB Linux
address-space ceiling. The actual helper retains its256 MiB ceiling/10-second
deadline; total experiment240 seconds. Every worker is ready-verified by the
independent parent before start and reaped before adoption. Per-parent timeout
is20 seconds. New output roots are exclusive; original fixtures and every
failure are preserved. These observations are not real-time performance claims.

`retain.py` is the original-run retention recipe, including its explicitly named
failed attempts. It is not a generic selector for arbitrary recording folders.
It copies only generated PCM/state/source/logs; it never includes executables.
The committed standard-library verifier inspects all retained outputs independently
without executing DSP, installation, audio or a VM. The next planner/adapter
experiment must satisfy ADR104's separately defined event/group contracts.
