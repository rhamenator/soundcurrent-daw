# Reusing prepared waveform coverage

Checkpoint 357 reduces repeated waveform decoding during nearby timeline pans
and zooms. It adds bounded extra coverage to new requests and selects suitable
already-admitted coverage before asking a worker to decode again.

## Coverage policy

The timeline first chooses the requested bin resolution under its existing
shared detail budget. `expandWaveformWindow` may then add up to one requested
span of nearby source frames, using only spare base-bin capacity at that same
resolution. It distributes the margin around the request, clamping and shifting
it at source boundaries. Empty requests stay empty. Checked division and
subtraction avoid overflowing capacity or endpoint arithmetic.

The helper never coarsens requested detail to create a margin. If the request
already consumes its bin allowance, it gets no extra coverage. The existing
512-source metadata, 48 MiB resident-model and shared base-channel-bin policies
still apply; these are viewport resource policies, not project track limits.

## Reuse and freshness

The cache query requires the same media path, source digest/format, root and
project-opening epoch. A candidate must contain the requested interval, provide
at least the requested bin resolution, and fit the current per-source bin
allowance. Nonempty models do not substitute for empty requests. Ready models
are preferred over loading or pending requests; within a phase, the smallest
admitted model is preferred. Faulted and closed entries are not offered.

The query performs no I/O, allocation of waveform models or worker launch. The
caller admits the returned actual key through the existing complete-view
transaction. Thus a covered movement can preserve the same ready model or the
same in-flight request. Offscreen ready data can serve a returning view while
resident capacity permits it. Requests outside retained coverage or requiring
finer detail still prepare new data.

Explicit source invalidation removes ready authority before any subsequent
reuse. New decoding still validates the entire pinned file, including format,
finite samples and SHA-256. Cached peaks remain historical data for the expected
source, as before; reuse does not establish that an externally modified file
still matches. No new project state, IPC version or localization strings are
introduced.

## Acceptance and limits

Tests check source-boundary and overflow arithmetic, exact model reuse without
another child launch, retention of an in-flight operation, and refusal across
source/path/root/epoch/detail/budget boundaries. Timeline tests exercise nearby
pan, zoom, uncovered movement, and return to offscreen prepared coverage. The
existing rendered crop and dense-source tests remain part of acceptance.

All five final Release suites pass in 15.86 seconds, including the full application
window and GUI resource checks. The three focused ASan/UBSan suites pass in
16.11 seconds with leak detection and halt-on-error enabled. The rendered crop
image is byte-identical to the visually inspected checkpoint-356 image. The
72-source view reserves at most 192,917,472 declared bytes, within its 256 MiB
ledger; this is not a measured RSS claim.

The [acceptance manifest](../tests/results/M2/2026-10-15-waveform-coverage-reuse/manifest.json)
binds source commit `2debed8`, the original logs, source overlay and executable
hashes. The preceding hosted run `38826291512` at `36013b7` completed successfully;
its 15 original terminal logs and 12 artifact ZIPs are retained before advancing
the PR. That prior-head result does not qualify the newer source-window/reuse
implementation on Windows.

This reduces work where coverage is reusable. An uncached request still performs
full-file validation; continuously moving outside the retained margin can still
cause new preparation. Durable peak storage, large-file policy and native Windows
qualification remain separate work. The installed previews and audio processing
are unchanged. Full DAW parity remains incomplete.

Next: obtain native Windows evidence for the new worker protocol and viewport
behavior, then deliver a matching preview. While hosted checks run, continue the
remaining M2 workflows. P010 track versions is an explicit gap: the current
`Track` model has active clips and retained take lanes, but no alternate
arrangement/version state. Its implementation must preserve group links, history,
media references and future automation ownership; it must not claim full P010
parity without those workflows.
