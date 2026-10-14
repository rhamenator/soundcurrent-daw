# Separate comp edges and bounded PCM assembly

Checkpoint 268 implements an off-audio assembly primitive and tests separately
rendered nonunity/pitched handles against preserved nominal audio. The boundary
measurements show that this rendering recipe is not ready for quality admission.

`assembleCompHandlePcm` emits the generated prefix, unchanged current buffer and
generated suffix in bounded slabs. It has ledger admission, input/output extent
bounds, finite/complete-read checks and cooperative polling around every I/O call.
Source readers must bind independently verified immutable media; the caller owns
publication, deadlines and partial-output recovery. The primitive does not attach
an artifact to a project or grant filesystem authority.

Both joins report the actual sample step and its deviation from the neighboring
first differences. A one-frame edge reports the step and explicitly lacks a slope
measurement. Metrics do not certify sound quality and no curve correction, phase
shift, padding or inner-buffer blending is applied.

## Acceptance and processing observations

The new C++ acceptance runs 37,098 checks across 1, 2, 4, 8, 32, 128 and 256 channels,
zero/one/multiple-frame edges and block sizes 1, 37, 511 and 65,536. It checks exact
sample placement, headroom, signed zero, join observations, invalid extents, resource
refusal, missing callbacks, incomplete/nonfinite reads, pre-cancellation and late
output failure with released scratch credit. Three focused Linux gates pass in
1.77 seconds, including the existing grid and independent Fraction checks.

[Frozen evidence](../tests/results/M2/2026-10-14-comp-edge-assembly/index.json)
binds implementation `004c099309378b068a79f286c09bd0fecb817d6c` to source/library/
executable hashes before and after execution. A dedicated Release build and serial
low-priority worker preserve preceding qualification trees.

The actual probe creates 36 offline R3 renders: 12 nominal artifacts, 12 prefix
context jobs and 12 suffix context jobs. Twelve stereo cases cover duration 1/2
and 3/2, independent pitch −1200/0/+700.007 cents, steady tones and transient trains.
Each edge has 26 output frames and real surrounding raw context; exact fractional
raw origins/intervals and compensated processing grids are retained in the recipe.
The assembled output is identical across 127/511-frame blocks. Independent Python
analysis checks all inner PCM bytes, cropped edge bytes, rational source recipes,
frame counts, headroom and measured boundary steps.

The largest join step is 2.907103 sample units; maximum deviation from neighboring
slopes is 3.096760. Every nominal inner buffer remains bit-preserved, but these
joins are unacceptable evidence for automatic seamless handle generation. PCM
preservation alone cannot qualify the recipe. No job is admitted or auditioned.

The archive contains all owned raw/nominal/context/assembled PCM, complete reports,
process receipts and source hashes: 7,863,153 bytes, 60 entries, SHA-256
`a34df2dc2ff59633f76f982b3c49478ff389f9b42eda9eadcca2b206e53b06b4`.
It records actual test/render/analysis processes 3707104/3707238/3707242; rendering
took 4.97 seconds. Native endpoints and full processing quality remain unqualified.

## Preceding hosted grid checks

Original [run 38564240790](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38564240790)
against `be91b084` passed all four jobs: 170 Linux tests, the Windows cross-build,
93 native Windows tests plus both explicitly executed grid gates, and 22 Windows
desktop tests. All original terminal APIs, direct job logs and five artifact ZIPs
are retained before updating the PR head. The artifact hashes match the server's
advertised digests. The independent Windows build-input audit verifies all 14
payload files, 1,536 source files under actual Windows checkout filters and both
pinned media DLL builds. Main-window/native-audio/installer flags remain false.
The new assembly gate is wired into native Windows CI; its current run is pending.

## Next work

Resolve boundary phase/context behavior with explicit quality evidence before
connecting a producer to owned worker requests/completions and paired admission.
Retain original raw/nominal assets, their anchors and transactional recovery; do
not equate metadata matching, a bit-preserved center or a forced boundary fit with
professional processing quality. Evaluate the preservation contract against the
actual requested crossfade workflow while documenting any proposed change to it.

General protected maps, formants, subsequent extensions, linked rates/layouts,
paired stretch regeneration, controller root/epoch/cancel/close/recovery and UI
integration remain required. Full frozen-reference, installed Windows and
European language completion remains open.
