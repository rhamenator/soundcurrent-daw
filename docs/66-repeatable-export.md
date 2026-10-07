# Repeatable default WAV exports

Date: 2026-10-07 UTC. Regression encountered during native manual-recording work.

Observation 30 preserves the original Debug suite result: 30/31 pass, with
`desktop-export-ui` reporting the combined sample/file-hash comparison failure.
Its exact executable/source/logs were frozen before diagnosis. Original individual
hashes were not logged, and its QTemporaryDir was removed on unwind; do not present
later replay files as the original failure project.

A separate debugger replay used that frozen executable, deliberately waited two
seconds before the independent comparison render, and retained its project before
intentionally killing the inferior after inspection. The replay's two 48,928-byte
audio data chunks have identical SHA-256. Complete files differ only at byte 60,
inside the PEAK chunk's timestamp (1791331713 versus 1791331715). This establishes
a reproducible header defect; it does not recover the unlogged original hashes.

The upstream libsndfile 1.2.2 sources enable a PEAK chunk by default for float WAV
and write current wall-clock seconds into that chunk: [WAV default](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/wav.c#L203-L210),
[timestamp writer](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/wavlike.c#L1158-L1168),
[command implementation](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/sndfile.c#L1012-L1053).

Default WAV export now disables the optional timestamped PEAK chunk before
writing samples. RF64 retains its default absence of that chunk. Peak,
over-full-scale counts and float headroom remain in
`ExportResult`; samples are unchanged. No timestamp-bearing metadata contract or
source-media format is altered. Future explicitly requested broadcast metadata
must have its own versioned, deliberate reproducibility policy.

Acceptance renders the same immutable snapshot to distinct destinations across
different seconds, compares actual samples and complete bytes/file hashes for both
WAV and forced RF64, and verifies headroom/project preservation. The desktop export
test also waits across seconds and separates sample versus file-byte assertions.
It now retains its project on failure. Full Debug/sanitizer and Windows compile
results are in the [dated receipt](../tests/results/M2/2026-10-07-native-manual-recording.json).

This fixes the reproduced default-export nondeterminism. Prior 29 observations,
their uncertainty and sustained/physical qualification gaps remain unchanged.

Observation 31 retains the first correction's Debug suite (30/31 pass,32.02 s),
frozen source/executable/logs and original combined repeatability assertion. Its
original format iteration/hashes were not logged and temporary project was removed.
A separate replay breaks in the initialized RF64 iteration: both data hashes match,
complete files differ only at byte108 in PEAK timestamps1791331873/1791331874.
That replay project is retained separately, with an intentional inferior kill
after inspection. The upstream command allocates peak_info when it is absent,
even for SF_FALSE; RF64's open path does not create it by default. The correction
therefore disables PEAK for WAV only and leaves RF64 untouched. A command error
closes the file handle before construction throws.

Both export acceptance programs now retain projects on failure and log separated
sample/file-byte terms or the failing format/hash pair. All31 observations remain
retained; original missing terms are not retrospectively claimed from replays.
