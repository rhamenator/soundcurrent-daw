# Editable protected stretch and uncommitted audition

2026-10-10 UTC. The full goal remains active and incomplete. This is an
experimental editing workflow in source, with synthetic integration acceptance;
current installed/native audio and broad acoustic qualification remain open.

## Workflow

Select a clip and open **Pitch/stretch**. Constant duration/pitch controls retain
their previous behavior. Enable **Experimental protected stretch markers** to
add, remove or edit original-source and target positions. Each marker has a stable
UUID; sorting by source position retains that identity. Coordinates refer to the
retained raw span at the physical source rate, independently of project rate and
linked playback speed. The table scrolls; unfocused spin controls ignore wheels.
The dialog uses a scrollable body bounded by the available display.

Render prepares an owned floating RF64 derivative in the supervised worker. It
preserves the raw asset and does not edit the project. **Prepare audition** builds
an ephemeral model on the playback control worker. Choose output channels and
**Play audition**; these audition route choices are temporary. The audition plays
the selected clip's interval through its existing track processing, with sibling
clips muted only in the ephemeral model. It uses the existing playback adapters.
No project/history edit occurs. A newer canonical revision retires audition;
Stop releases its endpoint before Apply becomes available.

**Apply verified result** commits one guarded structural edit. Undo/Redo retains
raw and derived assets. Save/reopen restores marker IDs, controls and exact
positions. Rerendering a cropped/split derivative maps its previous visible
output interval back to raw coordinates, then into the new map. It never treats
the old derivative as raw input. Fade envelopes remain affine, clip-relative
shapes retimed by new visible length / old visible length, with signed exact
floor/ceiling anchors; they are not raw-position warp envelopes.

For a first warp, the original clip's exact source map defines its visible end.
The prepared raw buffer may round upward to cover a partial physical frame;
that rounded buffer extent does not enlarge the visible interval. Independent
44.1/48 kHz and linked 3/2 or 5/4 rate fixtures preserve an 8,193-frame identity
interval, and use separately calculated nonlinear length oracles. The identity
regression fails before the endpoint fix; persistence and Undo/Redo preserve it.

## Explicit first-mode limits

The processor identity is
`soundcurrent.stretch-transient-protected-rubberband4-r3-v1`. It requires zero
pitch, preserved formants, no extra context, integer acoustic marker coordinates
and an integer raw origin. Exact rational planning/state can represent fractions;
the acoustic worker refuses them rather than quantizing them. Constant/context
stretch remains available independently.

Protected spans default to 256 frames before and 2,048 after each marker, with
64-frame actual-source blending halos. The exact geometric planner admits a
64-frame minimum nonunity gap. The production acoustic adapter additionally
refuses **nonunity source or target gaps below 2,048 frames**, before creating an
operation directory. That matches the shortest gap in the retained synthetic
bank; it is not a processing-quality guarantee. Unity gaps copy prepared raw
samples exactly. Nonunity gaps use offline R3 in bounded 512-frame I/O blocks,
with no whole-file input/output arrays and no padding/truncation repair.

Mono/stereo uses Together; discrete multichannel uses Apart. Supported format
admission is 8–192 kHz and 1–256 channels. New comparison evidence covers 48 kHz
stereo/eight-channel fixtures only. The known sustained-gap relative-phase
failure, ambiguous background-bed arrivals and listening qualification remain
open. The mode is unsuitable for a phase-preserving group claim. General warp,
automatic transient analysis, tempo maps, pitch/formant combinations, comping and
all remaining milestones remain required.

## State, identity and ownership

Schema **1.15** adds optional warp state. Readers migrate 1.14 context and earlier
constant anchors without changing their keys or processor identities. New state
persists original marker owners/positions, protection/chunk/map policy, derived
semantic span roles and normalized map points. Evaluation-only scratch UUIDs are
not persisted. Decoding verifies the entire normalized representation, including
exact integer types, rather than trusting derived boundaries from a file.

Only this mode uses request **v5**, a 256 KiB envelope. Legacy v4 retains its exact
20-field shape, 16 KiB limit and cache identity. The new cache key includes raw
source identity, origin/span, target, processor/channel policy and normalized
warp state. Duplicate nested keys, forged boundaries/owners/types, unsupported
coordinates and changed raw media refuse. Worker cancellation preserves owned
incomplete intent; publication requires exact duration, full source/audio/sample
hash checks and a verified completion marker.

Planner, codec, queued/prepared/result marker copies, GUI rows and playback
models reserve declared payload credit before allocation/copy. The table caps
at 1,023 rows with bounded text editors; geometry/protocol/resource admission may
refuse earlier. These charges are conservative payload accounting, not exact
allocator/RSS measurements. The disposable worker has an OS memory ceiling and
hard deadline; opaque vendor allocations are contained by that ceiling. GUI,
codec, file I/O, allocations, ledger locks and final ownership retirement remain
outside audio callbacks. Playback/export consume the verified prepared derivative
through the existing shared engine.

## Acceptance and provenance

- [State tests](../tests/stretch_state_tests.cpp): independent 39/23 and 23/39
  coordinate oracles, cropped nonlinear/constant conversion, normalized-state
  tampering, signed fades, legacy migrations, exact Undo/Redo and overflow refusal.
- [Worker tests](../tests/protected_warp_worker_tests.py): actual supervised v5
  children; all 18 regular-bank complete PCM outputs match the unchanged PR89
  prototype; nonzero raw origin, endpoint/touching spans, short-gap refusal, cancellation, changed
  raw media and forged state are exercised. Linux changes raw media after ready;
  Windows checks held-reader mutation denial and rejects a changed source hash
  before opening a job. These are distinct platform observations.
  Stereo/eight-channel parent verifiers
  perform adoption, live seek/split/crop, export, save/reopen and callback audits.
- [Qt workflow](../tests/protected_warp_ui_tests.cpp): actual worker, marker
  identity/editing, resource refusal, complete audition PCM, unchanged canonical
  state/routes/history, endpoint retirement, Apply/Undo/Redo/save/reopen/export
  and invalidation by a newer canonical revision. Its endpoint is synthetic;
  this is not native audio-device or installed application evidence.
- Existing v4 worker/controller/UI and exact arithmetic workflows remain gates.
  CI adds the new worker/state workflows to native MSVC and the new UI workflow
  to native Windows Qt tests, separately from Linux and cross compilation.

The new planner code is an adapted copy of original GPL project sources from
[PR89](https://github.com/rhamenator/soundcurrent-daw/pull/89), merged as
`471f975bd47e5c7385ca1e0c0c2b31e49ca22b97`. Production types use distinct names;
codec, streaming assembly, state integration and UI are new. Frozen experiment
files and their retained source bindings remain unchanged. Rubber Band 4.0.0
inputs/notices remain unchanged under the existing reviewed license inventory.
Original synthetic sources are project-owned GPL fixtures; no proprietary
content or algorithm was copied. Equalizer repositories were not changed.

New controls/errors are extracted into the existing contextual Qt catalogs, with
translator notes for source/target timing domains and uncommitted audition.
Existing translator work is preserved; unfinished entries remain unfinished and
use source-language fallback. This does not qualify European translations or UI.

Functional, quality, content and native-project compatibility family gates remain
unpromoted. Source CI is not installed/native/listening qualification. Before
shipping this workflow in a product preview, qualify the exact source on native
Windows, refresh local Linux/Windows installer/source pairs, and exercise installed
render/audition/Apply/Undo/reopen/export and refusal/recovery. No product binary or
release upload is authorized by source-backup publication.
