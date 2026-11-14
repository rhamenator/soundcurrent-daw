# Isolated waveform worker and bounded reply

Checkpoint 342 adds `sc-waveform-worker`, the first process boundary for timeline
waveform preparation, and a Qt-independent reply codec. The GUI does not launch
this worker yet; timeline drawing and installed previews remain unchanged.

## Request and result contract

The worker accepts an explicit absolute approved root, portable relative media
reference, expected SHA-256/frame/rate/channel identity, base-bin width, operation
UUID, project epoch, request generation, source-byte limit and memory allowance.
It opens media through `ApprovedMediaRoot`, performs checkpoint 341's full pinned
source validation and constructs all peaks before writing stdout. It neither
writes project state nor creates output files. Unicode command arguments use the
native Windows wide-character entry point; Windows stdout is explicitly binary.

The version-one reply is a 156-byte header followed by little-endian IEEE binary64
minimum/maximum pairs in level/bin/channel order. The header binds the operation,
epoch, generation, actual child PID, expected source identity and base-bin width.
There are no untrusted allocation lengths or layout offsets. The parent derives
the exact wire size and complete pyramid layout from its trusted request and
policy before reading or allocating a result. Empty sources have only a header.

The decoder requires exact bytes, current ticket and independently observed PID.
It checks finite, ordered extrema and verifies every coarse bin against its finer
children, including odd tails. A failed decode returns no partial model; reserved
credit is retired. Encoded bytes and decoded peaks have separate resource leases
so simultaneous ownership is charged. Moves leave the source byte wrapper empty.
Overflow, resolution, frame/channel and payload limits are checked before array
allocation. These grants bound owned payload, not allocator overhead or process
RSS. Cancellation is cooperative during codec loops.

The reply is not an authentication mechanism. A matching digest field alone does
not prove that an arbitrary executable decoded that source. The controller must
launch the shipped worker, supervise its process, require normal exit and empty
stderr, enforce an end-to-end deadline and recheck current session/source identity
immediately before publication. The receive bank must be admitted before draining
stdout. Excess bytes, cancellation, errors, timeout and stale results must retire
the exact child and all retained buffers. Those controller responsibilities are
the next implementation step, not claimed complete here.

The worker supports the approved portable-reference route. Explicit native-leaf
selection and additional media formats remain separate future work. Geometry is
still in source frames: no processed, faded or resampled waveform is claimed.
No decoder work belongs in painting, the GUI thread or audio callbacks.

## Verification and retained failures

Release and ASan/UBSan each pass four focused suites:

- 21,060 protocol checks, including an independently constructed wire format and
  direct original-sample comparisons for decoded bins, every header-byte mutation,
  truncation/trailing bytes, PID/epoch/generation mismatch, non-finite or inverted
  extrema, inconsistent coarse levels, arithmetic/resource refusal and cancellation.
- 53 real worker processes, each with an original ten-second test deadline. Cases
  include 1/2/257/1,024 channels, empty/one-frame/odd-tail sources, three resolutions,
  Unicode filenames, floating headroom/precision, bad source identity and arguments,
  missing/traversing media, insufficient memory and non-finite final samples. The
  independent Python oracle compares every output byte against direct PCM scans;
  failures must publish no stdout, and source bytes/files remain unchanged.
- The existing 164,507 source-pyramid and 20,956 pinned-WAVE checks still pass.

Final CTest durations were 0.71 seconds in Release and 4.07 seconds with ASan/UBSan,
including leak detection. Builds used one low-priority worker and no VM or physical
audio. Native Windows build/test coverage was added to CI but has not run on this
change; the wide-character/binary-output path remains independently unqualified.

The first real worker test failed because a project-asset validator required a
`media/` prefix even when the approved root already named that directory. The
worker now delegates relative-reference validation to `ApprovedMediaRoot`, which
owns that contract. The original failure log and fixture are retained alongside
the subsequent passing evidence; no path-safety rule was relaxed in that class.

The preceding PR run `38801059137` at `2cfec5e` is still active. Its completed
Windows comp job failed the same-root reopen/stale-owner assertion in
`desktop-comp-assembly-apply`. The import/localization aggregate job also failed
because its upstream desktop result failed; this is not a second observed test
failure. Full original logs for both jobs are retained. Their failures predate
this waveform change and remain unresolved; investigate before the next preview.

See the [evidence manifest](../tests/results/M2/2026-10-15-waveform-worker-protocol/manifest.json).
Next: inspect the retained stale-owner failure, then add the bounded asynchronous
waveform controller, viewport cache/invalidation and channel-aware drawing. Full
M2 editing and the full DAW scope remain incomplete.
