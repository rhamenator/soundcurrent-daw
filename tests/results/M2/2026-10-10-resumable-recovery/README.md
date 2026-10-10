# Linux retained-render paging acceptance

[Checkpoint164](../../../../docs/164-resumable-render-recovery.md) defines scope.
Source `a8bf340ed233daaffc48523774000e776ba0c397` includes the code and tests
compiled for final local acceptance. Final logs report 831 controller assertions,
276 UI assertions and168 approved-root assertions; these are assertion counts,
not parity workflows. Synthetic manifests and offscreen UI do not qualify many
completed renders, installed paging, native Windows, audio endpoints or quality.

The archive retains original missing-test-include compilation and incorrect
CTest-selector failures, earlier intermediate logs, final rebuild/test logs and
309 trusted Git source-input bindings. Intermediate logs are diagnostic evidence,
not relabeled qualification of the final source. No product executables, personal
media, credentials or transport are included. No VM was used.

```sh
python3 tests/results/M2/2026-10-10-resumable-recovery/verify.py
```

The exact recorded source Git object must be available. Verification reads Git
blobs and retained files; it executes no archived code, GUI or audio.
