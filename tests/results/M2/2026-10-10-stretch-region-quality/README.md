# Retained explicit-region quality diagnostic

Owned synthetic Linux observation, production basee73eef65, successful
experimental sources uncommitted at execution. See [checkpoint145](../../../../docs/145-stretch-region-quality.md)
and [ADR104](../../../../docs/decisions/104-local-warp-and-group-quality.md).

`capture.zip` contains427 hashed members /29,679,603 uncompressed bytes:

- Five original generated48 kHz float sources and all63 complete processed WAVs.
- Thirty-seven actual shared-reader/export context and whole-source crop pairs.
- Six full sustain exports supporting24 FFT observations.
- Each job's intent/completion/start and saved project; all289 successful process
  outcomes (63 helpers,226 parents), exact source/build/library/executable hashes.
- Exact successful C++/Python/audit sources; final and earlier build/run logs.
- Initial missing development-link failure, completed helper followed by parent
  export-path refusal and initial8 MiB retention cap refusal. Prior failed
  experimental source text is not reconstructed or claimed to be retained.

Manifest binds the9,673,174-byte archive and every member. All recordings and
generated content here are original GPL-3.0-only fixtures; no proprietary content,
personal recordings, credentials or executable payloads are included.

Run from any checkout with Python3 (no NumPy/build/Qt/audio runtime required):

```sh
python3 tests/results/M2/2026-10-10-stretch-region-quality/verify.py
```

The initial full inspection passes7233 checks. It independently checks hashes,
finite sample/frame geometry, render key/request bindings, recorded ready/start/
terminal/adoption ordering, exact integer/unity PCM, per-crop RMS/peak/energy/
correlation and a separate standard-library FFT. Recorded parent Undo/reopen/
callback outcomes are inspected, not replayed. Windows native behavior, audible
quality, Q-STRETCH, Q-PITCH and installer qualification are explicitly false.
The matching whole-source render is diagnostic, not acoustic ground truth.

To reproduce actual DSP, build the isolated parent as described in the experiment
[README](../../../../experiments/stretch-region-quality/README.md). Use a new
owned output directory. Retain failed attempts separately; do not overwrite the
original observations or relabel the exact-source historical receipts.
