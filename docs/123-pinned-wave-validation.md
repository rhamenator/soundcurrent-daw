# X004: pinned WAVE metadata and content checks

Date: 2026-10-09. Frozen baseline SC-DAW-BASELINE-2026-10-05 unchanged.

## Implemented increment

`sc-wave-validation` is original GPL-3.0-only C++20 and independent of Qt/the
audio engine. It borrows one explicitly approved, pinned plain file, shares that
file's resource ledger, and performs serialized control/I/O work. It neither opens
a foreign pathname nor creates/copies a destination asset.

This revision admits RIFF/RIFX WAVE with unsigned8/signed16/24/32 PCM or IEEE
float32/64, plus little-endian WAVEFORMATEXTENSIBLE with the same subtypes and
full container precision. It preserves original rate, channel count, sample
precision, endian/container identity and original channel mask. No resampling,
channel remapping, correction, clipping or conversion is performed. PCM is
normalized to doubles; finite floating-point headroom remains above unity.

An original preflight checks the declared container extent against the approved
file, chunk extents/padding/counts, format/data uniqueness/order, byte rate/block
alignment and exact integral frame extent. Unknown bounded chunks are skipped by
extent, not loaded or executed. WAVEX masks are preserved and checked against
channel count; full-precision PCM/float GUIDs are explicitly matched. This prevents
the decoder's tolerance of some damaged files from becoming our completion claim.

The existing libsndfile then uses
[SF_VIRTUAL_IO](https://libsndfile.github.io/libsndfile/api.html#virtual)
over the object's positional read method. Virtual callbacks are noexcept: caught
errors stay in the context and are rethrown on the C++ caller after library calls.
There is no CRT descriptor crossing or library path reopen. The library's own
metadata must agree with preflight. Every admitted frame is decoded, checked for
finite samples and measured; EOF/error state and close must pass. A final streaming
SHA256 and unchanged-object check bind the report to original file bytes.

## Explicit limits

Trusted policy bounds decoded frames, channels, chunk count, requested read bytes,
callback/read operations and block frames. The file's original byte limit is
required at root admission. A256KiB decoder-work allowance and the exact double
block payload are admitted before their work/allocation. Ownership retires off
audio. The report's read counters cover our reads/hash and virtual interface
operations, not every kernel metadata syscall or decoder instruction.

These are cooperative I/O/payload limits, **not hard RSS, parser CPU, wall-clock or
OS sandbox limits**. libsndfile owns internal allocations and may spend time between
callbacks. Before connecting foreign WAVE checks to the GUI, use a child process
with bounded output, parent deadline/cancellation and terminal retirement; evaluate
OS memory limits separately. The observer borrows one block and cannot publish a
complete asset until successful return; later content/hash/mutation may still fail.
Linux identity/time checks do not create a hostile-filesystem snapshot.

The current decoder's documented theoretical ceiling is1024 channels. The owned
fixture actually checks1024 discrete channels, independently of the engine's
current channel layouts. This is a provider/admission limit, not a project track
limit or completion of the arbitrary-channel goal. Other providers/layouts remain
required where the selected implementation cannot meet the full requirement.

RF64/W64, compressed WAVE, partial-valid-bit WAVEX, big-endian WAVEX, multiple data
segments and broader audio formats are explicit follow-up gaps. Current checks
refuse unsupported cases; they do not erase source properties, rewrite media or
silently substitute another codec. Whole-container length/order constraints also
need a broader valid-file corpus. No complete WAVE/BWF metadata preservation,
speaker interpretation, native-project conversion or render equivalence is claimed.

## Runnable development tool

`sc-approved-wave-probe --root ABSOLUTE_DIRECTORY --relative PORTABLE_REFERENCE
--maximum-bytes N` returns one ASCII JSON record with protocol
`sc-approved-wave-validation-v1`: exact reference, original byte count/hash,
frames/decodedFrames, rateHz, channels, bitsPerSample, channelMask, containerId,
extensible, encodingId, peakLinear and work counters. Encoding IDs1..6 are stable
unsigned8/signed16/signed24/signed32/float32/float64. Failure produces no success
stdout and a bounded stable diagnostic on stderr. The root path is not printed.

The tool uses16MiB of declared resource admission and the API's trusted defaults:
1,000,000,000 frames,8GiB requested reads,1,000,000 I/O operations,4096 chunks,
1024 channels and256 frames/block. These current check policies can refuse a large
file; future product controls must expose appropriate trusted limits. This tool is
development-only and is not installed or invoked by the current desktop inspector.

## Evidence and qualification

| Acceptance workflow | Observed evidence | Remaining gate |
|---|---|---|
| Independent original PCM8/16/24/32 and float32/64 bytes; both endian variants |20,508 Linux checks, including full sample oracles | Native MSVC and broader source corpus |
| Float headroom, ordinary/WAVEX masks, nonstandard rate, zero frames,1024 discrete channels | Same owned checks, exact frame/sample/metadata comparison | Other providers/layouts, interpretation and conversion |
| Truncation/extent/alignment/codec/non-finite refusals, work/byte/operation quotas, observer failure, cancellation and retirement | Release + ASan/UBSan; leak detection disabled | Hard child deadlines/memory limits/storage faults |
| Actual Unicode argv, binary fixtures, independent Python SHA/peak, two frozen REAPER media files unchanged |64 actual child checks | Native Windows runtime and UI report validation |
| Desktop root/replacement choices, copied destination, Undo/reopen/aligned renders | Not implemented by this increment | Next user-visible workflow and full adapter requirements |

Four selected Linux Release and sanitized tests pass (root/probe + WAVE/content
probe). Source, binary and raw log hashes are retained in
`tests/results/X004/2026-10-09-pinned-wave-validation`. Native WAVE tests extend the
existing protected Windows headless context without removing another test/check;
the cross-build with media disabled makes no WAVE compilation/runtime claim.

The first PR67 head17c600ccf7293734d37b1b61479c64f23cac06ea passes actual native
Windows20,503 WAVE checks/CLI64 in run37889809849. Artifact11597887600 has verified
SHA256 2066bb5d6019fab0d1d9233ece6c79fea5ba10e840c5ecbbf651aa070275c3d1;
its original metadata/ZIP/raw log is retained. The five additional Linux checks
are the Linux-only mutation fixture, not a skipped Windows test. A subsequent
test-only refinement adds lowest PCM bits and float32/64 adjacent representable
values to the independent sample oracle. Its Linux20,508/CLI64 gates pass Release
and sanitized; the final native cohort remains separate and pending. No decoder,
policy, format or product behavior changed in that refinement.

The final PR67 head1ad6153c67dccaf03188d10665f1f693695f583b subsequently passes
native20,503 WAVE/64 CLI checks in run37890424413 and mergesfa6b380 after all four
required contexts succeed. Artifact11598123196 digest is
8329c1ce3a08768be6ab63c362d4d2ea2614f0c1314fd9cffee0b66f740e0f7b;
the exact metadata/ZIP/raw log is retained with checkpoint124 as prerequisite
evidence. That final precision cohort is separate from the new desktop/report code.

The prerequisite PR66 merged41e38af after exactbd9e8cb passes all four protected
contexts in run37888375635. Its actual native folder162/CLI23 checks include owned
NTFS case-distinct file and intermediate-folder fixtures. Artifact11596959423 has
verified SHA256 e25275d54fd007c94e55c2dce46a8aaff0f52ea6b4d473e79c7a320a0e258eda;
its metadata/ZIP/raw log stay separate from this new WAVE code. No earlier wrong-file
read was observed; the explicit case-folding request was removed as a reviewed
policy improvement. No local VM, audio route, equalizer or installed preview changed.

## Next concrete task

Independently validate this child report, bind validated SourceFile ID20/object
identities to original references and explicit root/replacement choices, and add
the cancellable desktop media checklist. Preserve missing/duplicate/ambiguous/
unsupported/unsafe/changed/invalid-audio evidence; do not choose an occurrence or
infer approval from saved paths. Add explicit selected-file/nonportable mappings.
Then qualify transactional verified copying into a new project, extended timing/
gain/fade/rate/pitch state, loss preview, Undo/reopen and aligned independent renders.
Refresh installable preview pairs after actual runtime/install gates. All registered
formats, Windows, European localization and frozen F/Q/C/N parity remain incomplete.
