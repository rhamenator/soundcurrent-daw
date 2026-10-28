# Independent comp media review

Checkpoint 272 adds disk-side integrity and processing-scope review of actual
output files. The review result contains the recipe digest, runtime context,
output declarations and comparison counts. It is not a worker-completion or model
adoption token. No source/project mutation, renderer launch, schema change,
installed-preview replacement or physical audio test is performed.

## File and PCM checks

`reviewCompMedia` verifies the current immutable recipe/context against the
session. It hashes and fully validates raw, current, nominal and retained-take WAV
files, and hashes every declared original-source companion against its expected
extent and SHA-256. Companion capture semantics remain a separate native gate.
Both outputs use new asset IDs and the recipe's operation directory:
`media/comp-results/<operation>/<asset ID>.wav`. Layout, frame count and rate must
match the derived domain; generated results must be finite float32 WAVs and cannot
inherit unrelated native-evidence declarations.

Pinned approved objects supply random PCM reads with bounded 64 KiB byte slabs.
The decoder handles the inspector's PCM/float encodings and byte order; the
qualified corpus below covers RF64 float32, float64 and PCM24 current files.
Source decoding, waveform validation and comparison run on a serialized disk or
control owner, outside audio callbacks and the GUI. Ledger admission precedes
scratch allocation. Byte limits and a cooperative deadline/cancellation policy
apply; this is not a process sandbox or a hard OS-I/O deadline.

For physical results, every decoded current sample outside the actual prefix/
suffix inner-blend intervals must remain bit-identical inside the expanded output.
The reviewer also runs the actual positioned resampler for original and expanded
media and compares every original project sample outside the requested transition.
For post-SRC results, it compares the project-rate result directly against the
independently resampled original fragment. These are pre-clip-gain/fade/EQ samples;
full mixed-output and live/offline overlay qualification remain open.

Current/result handles are checked unchanged after comparisons. Raw/current/
nominal/retained sources and result hashes are rechecked before completing the
pair. The existing immutable owned-filesystem contract remains required. Existing
media accepts no generated output. Either fragment's failure prevents a completed
review object. Original source and session state remain unchanged.

Inside permitted intervals, this stage does not certify curve conformance,
producer alignment, perceived sound quality, or job ownership. Those checks must
be combined with this scope review before producing an adoption token. The old
unity verifier/model gate remains unchanged.

## Actual source-bound acceptance

[Receipt](../tests/results/M2/2026-10-14-comp-media-review/index.json) binds
implementation `6b0fa05c65d3f0b405c66479c4e126bc750fd7fc`, source/library/executable
hashes, CTest PID 3790120 and actual fixture child PID 3790169. Three selected Linux
gates pass in 18.72 seconds: 4,894 media-review checks, 618 recipe checks and 33,054
processing-permission checks. The fresh dedicated Release build uses one low-priority
worker with media enabled and desktop/PipeWire disabled.

The file corpus contains 36 paired cases: 6 physical and 66 post-SRC fragments,
plus a separate existing-media pair. It uses the immutable owned 36-render R3 bank
from checkpoint 268; no new R3 rendering is performed. Source rate is 48 kHz;
project rates are 44.1/48/96 kHz. All nine combinations of project rate and current
encoding (float32/float64/PCM24) are covered. Time ratios 1/2 and 3/2, pitches
-1200/0/+700.007 cents, two stereo signals, independent playback speed 3/2 and
fractional playback origins are exercised.

The review compares 229,280 unchanged physical samples and 2,842,244 unchanged
project samples bit for bit. Candidate samples use retained R3 contexts as
controlled fixture data: their producer alignment and curve/sonic quality are
explicitly unqualified. Synthetic schema-shaped original companion files test
byte/extent binding, including a same-length corruption; they are not native
recording receipts. Owned input-bank hashes are unchanged. Decoder/fixture shape
errors found during development were corrected without relaxing model validation.

Actual Unicode-root files, missing/wrong output references, same-format output
corruption with an updated file hash, changed retained media/companion bytes,
stale epoch/clip, cancellation, tiny result-byte/memory grants, existing-media
refusal and ownership retirement are tested. All final WAVs, project/recipe
snapshots, logs and process receipts are retained.

The original full-file archive is retained locally: 62,285,826 bytes, 368 entries,
SHA-256 `6bc3b53ddd080d82e2b0dbcd93d6b0d161974066326823ffeb5f7ba08f80363a`.
The public archive deduplicates identical payloads into 190 hash-named blobs;
`files.json` maps all 368 original paths to their bytes/hash/blob. Every original
payload was independently compared byte for byte during packing. Public ZIP:
16,348,607 bytes, 192 entries, SHA-256
`c73ccd9e3f6c6e72437a2f477618fb0a1c91e64a4d8360c364aab3e349154ff5`.
Use `files.json` to inspect or reconstruct a recorded file from its blob.

## Original Windows failure and correction

[Run 38587280851](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38587280851)
at `d4e77e0` has three successful jobs (Linux 174, Windows core cross-build,
Windows desktop 22) and one failed native job. MSVC reports C2666 at the recipe
JSON/string-view comparison. Native recipe execution and the subsequent bulk
native gate are not qualified by that run. The comparison now checks the JSON
string type and compares its typed string directly. Local recipe regression
passes; native confirmation requires the new source's original CI run.

[Original retained review](../tests/results/M2/2026-10-14-comp-media-review/pr194-recipe-ci/index.json)
includes terminal APIs/full logs and all five original artifact ZIP digests. The
ZIPs are retained locally before any head update. Independent build-input review
verifies 14 payload files, 1,566 corresponding source files with actual Windows
checkout filters, collection hashes and both pinned media DLL builds. Main-window,
native-audio and installer flags remain false. The compiler failure is retained,
not waived or relabeled through an unchanged rerun.

## Next integration and remaining scope

Implement a general owned span producer/request/completion path that binds the
trusted recipe and executable identity. The existing stretch worker handles
nominal pitch/time and protected warp; the comp-handle bridge currently supports
unity maps. General comp maps need exact fractional processing-grid geometry and
independently checked output/curve alignment. Post-SRC needs a prepared overlay
shared by live/offline execution or an explicit new representation, rather than
the old same-rate window schema. Preserve original anchors/assets, paired Undo,
stale-root/epoch checks, cancellation and durable recovery.

Broader channel/container/rate/group/native coverage, curve conformance, worker/
controller/project/UI integration and unresolved join/pitch/formant/phase/transient/
listening quality remain open. GUI sources are unchanged; localization check
remains 1,038 keys/34 partial catalogs/3,168 drafts/zero reviewed or fully UI-qualified.
Full M2–M11/F/Q/C/N/X004/X005, Windows installation and all-Europe requirements
remain active and incomplete.
