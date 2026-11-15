# Explicit comp join processing region

Checkpoint 269 evaluates [ADR157](decisions/157-explicit-comp-join-blend.md): retain
original raw/nominal assets and untouched derivative samples, while allowing a
declared blend inside the crossfade's explicitly permitted processing region.
The existing model/media verifier and project schema remain unchanged.

The separate `blendCompHandlePcm` primitive validates prefix/suffix permissions,
widths, generated-edge presence and nonoverlap before I/O or scratch admission.
A shared complementary quintic curve applies to every channel. All current PCM
outside the blend intervals and all generated outer edges remain bit-identical.
Original readers are immutable; callbacks, finite/complete reads, cancellation,
resources and partial-output handling retain the assembly contract. An empty blend
specification preserves the strict assembly operation.

Supplying physical permissions to this primitive does not authenticate an operator
edit. Production preparation must derive them from the immutable transition plan,
root/epoch and exact clip map, including SRC neighborhoods. That integration and
portable worker/state identities remain required before adoption.

## Exact-source observations

[Retained evidence](../tests/results/M2/2026-10-14-comp-explicit-blend/index.json)
binds source `4c4c0745c074fe89574c5875f41989ddfa57558f` to source/binary hashes,
actual processes and unchanged checkpoint268 input bytes verified against their
original archive. The new core checks pass 4,072,848 assertions for scope,
complementary channel curves, partition independence, untouched bits, overlapping/
invalid permissions, resources, cancellation and I/O failures. The strict assembly
regression's 37,098 checks also pass. Both core gates take 1.49 seconds.

The probe makes 36 assemblies from the original 36 R3 renders, with no new render:
12 cases × widths 26/128/1024. It retains both original inputs and new outputs.
Independent whole-PCM curve error is at most 1.192e-7; every outside sample and edge
matches exactly. Maximum join step decreases from 2.9071 to 0.7102; maximum slope
deviation decreases from 3.0968 to 0.2664. These measurements support evaluating
the explicit processing-region contract, not relaxing quality admission.

The artifact observations are mixed. Same-window RMS ratios range from 0.8554
to 6.6049 relative to the old nominal boundary; a nearly faded-out old artifact can
make that relative increase large. High-frequency energy ratios range from
0.0000128 to 104.97. The latter case increases a small baseline from 0.02137 to
2.24287 in the diagnostic FFT's energy units. Smoother joins alone therefore do
not establish artifact-free or professional quality. Actual pitch/formant,
transient, grouped-phase and listening gates remain open.

The self-contained archive is 12,239,071 bytes with 103 entries, SHA-256
`7c76a2582c3ae6bfd2e30dc71d2e5176fb7a2d29c2bd1ea316e14cf761614e72`.
Source, binaries and retained inputs are checked before/after all executions.
No new endpoint, VM, project adoption or installed preview is qualified.

## Original Windows failure and corrected GUI observation

Original [run 38567970097](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38567970097)
against83360529 passed Linux171, Windows core cross-build and Windows native93
plus three explicit grid/assembly gates. Desktop Windows passed21/22 and failed
`desktop-import-media-ui` after14 checks with “GUI stopped servicing events during
media work.” Original terminal APIs, full job logs and all four available artifact
ZIPs are retained; preview collection was skipped after this failure.

The old assertion required a one-millisecond timer pulse before an ordinary short
media task retired. Zero pulses do not distinguish a quick completion/fault from
a stalled GUI; the failure log contains no subsequent media-outcome diagnosis.
The corrected test checks actual media outcomes and measures dispatch while the
existing held child is busy, has a PID and lacks a terminal exit. Two precise timer
pulses must arrive during that active operation, for both check and copy paths.
Existing deadlines and close/retirement/commit assertions are unchanged.

The corrected Linux desktop gate passes75 checks in0.84 seconds. The source-bound
receipt records this actual run. It does not prove the new Windows test until its
hosted execution passes. Original run38572570686 now passes that corrected Windows
gate and all four jobs; checkpoint270 retains its complete evidence. Product UI
source is untouched; catalog check remains
1038 keys/34 partial catalogs, zero native-reviewed or UI-qualified languages.

## Next work

Verify the corrected Windows gate and retain its original results. Derive trusted
processing permissions from the requested transition and test general rates,
fractional origins and filter neighborhoods. Specify versioned render/state keys
and independent review for any authorized inner processing before changing model
adoption. Continue quality evaluation without promoting these diagnostic metrics
to the frozen professional acceptance gates. Full worker/recovery/UI integration,
frozen-suite/Windows installation and all-Europe delivery remain incomplete.
