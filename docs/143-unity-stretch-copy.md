# Unity pitch/stretch preserves the prepared source

2026-10-10 UTC, frozen SC-DAW-BASELINE-2026-10-05. This is a bounded M2 quality
increment following [the short-span experiments](142-short-clip-context-and-alignment.md).
Duration 1/1 and pitch zero now select a positioned copy rather than constructing
the phase processor. The existing dialog still requires Render, review and Apply;
the change is one undoable edit and retains original media.

## Processing and identity

`soundcurrent.stretch-positioned-copy-v1` copies the source prepared by the existing
`soundcurrent.src-positioned-best-v1` kernel. An integer origin copies floating
sample bits; a fractional origin deliberately uses the existing positioned
resampling contract. Fractional preparation is not a promise of bit equality to
an integer source slice. Formant selection remains saved but does not process a
unity copy. Linked playback speed, project versus physical sample rates, fades
and clip gain retain their separate playback/export behavior.

The copy route admits one-frame spans, 8–384 kHz physical source rates and up to
256 discrete channels within the existing input/output/memory/deadline grants.
It preserves finite floating headroom, channel order and exact duration. The
helper retains source revalidation, cancellation, exclusive operation directories,
ready/start handshakes, atomic completion and independently verified adoption.
It performs disk operations outside the audio callback. The parent still reserves
the aggregate child budget; no cheap-copy accounting bypass was added.

Other ratios or nonzero pitch keep
`soundcurrent.stretch-rubberband4-r3-positioned-v2`, its prepared minimum input
window and 192 kHz maximum. Very short non-unity spans remain explicit refusals
before destination mutation. Automatic context extension, dynamic warp maps,
segmented pitch and transient/formant/spatial quality remain open.

## Project and helper compatibility

Schema **1.13** stores the actual processor ID in every stretch anchor. Versions
1.0–1.12 remain readable. Existing R3 unity derivatives keep their original
processor, render key and audio; loading or saving does not regenerate or
reidentify them. An explicitly requested new render selects the copy algorithm.
Unknown IDs and copy IDs with non-unity settings are refused. Retained state and
playback memory accounting include the new owned string.

The helper protocol advances to **sc-stretch-render-v3** with an explicit processor
field. Parent and helper bind that field to the settings and artifact key. Version
2 helpers cannot serve new requests. New package qualification requires receipt
format **sc-stretch-worker-qualification-v2**, protocol 3 and the seven short-copy
and exact-sample acceptance observations. An explicit trusted protocol-2 argument
exists only for historical evidence inspection; package defaults cannot downgrade.
The retained f9a6353 Windows preview remains qualified for its historical source,
not these new production changes. No refreshed installed preview is claimed.

## Acceptance

The owned worker tests compare source/output sample bytes at integer origins,
exercise the fractional-origin oracle and equivalent rational origins, test six
short unity spans across the prepared boundary matrix and one 384 kHz/256-channel
one-frame copy. Its source includes negative zero, signed subnormal samples and
headroom values. Invalid processor/settings/protocol combinations must refuse
before creating an operation directory; cancellation and resource limits remain
tested. The existing non-unity artifact oracle covers shared live/offline samples.

The desktop test adds an actual default-helper one-frame render, verified review,
Apply, raw/derived identity, Undo/Redo, save/load and shared WAV export. A separate
RIFF parser compares the exported stereo sample bytes to the original source.
State tests retain legacy R3 identity and exercise schema and settings refusals.

The [retained local observations](../tests/results/M2/2026-10-10-unity-stretch-copy/)
include 397 implementation/test/build input hashes, executable identities and raw
logs. The 384-target Release build completed. Damaged Ninja dependency metadata
then caused a duplicate rebuild; that was stopped. The routing test and corrected
UI test were rebuilt with the exact CMake-generated compile/link commands. The
ignored build cache needs repair/replacement before the next build.

The registered 112-workflow run passed 111 and correctly refused an invalid
export location in the new test fixture. Only that fixture path changed; the
rebuilt UI workflow then passed **67 checks**, including the actual one-frame
copy/export and active-close tests. The original failed/aborted runs and stale
initial UI/state observations are retained separately. State **68**, controller
**48**, worker **241/41 completed jobs**, artifact **1045** with callback audit zero,
and **46** package receipt refusals pass. Five additional retained-evidence/package
inspections pass, including eleven historical installed-evidence refusal cases.
The verifier runs in CI without replaying a helper, native device or installer.
This is local working-source evidence, not a clean-commit native package receipt.

Native Windows and installed qualification for this source remain pending. No
physical audio, binary upload, full processing-quality parity or delivered
European language coverage is claimed. The full completion goal remains active.

## Next implementation

Specify a versioned processing region distinct from the visible clip crop, real
source context and asset-edge policy, exact output mapping and preserved raw
anchors. Use the retained counterexamples to define tests before enabling short
non-unity renders. Playback and export must consume the same verified artifact;
future event anchors require bounded, normalized maps and explicit quality gates.
