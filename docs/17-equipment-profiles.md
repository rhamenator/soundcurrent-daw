# X005 equipment profiling and profile editor

Owner requirement added 2026-10-05; [machine-readable inventory](../research/equipment-profile-requirement.json). **Partially implemented: the offline library/editor is in the Qt desktop preview. Monitoring/print routing, portable profile pins and full qualification remain required.** This supplements the frozen combined-reference scope; it does not modify that baseline or turn equipment coverage into a parity claim.

The owner is adding equipment profiles/import and a curve editor in the separate **SoundCurrent equalizer development** chat. Its current scope includes speaker and microphone imports, amplifier references, broad published-measurement discovery, brand/family/model browsing, visible response curves, adjustable correction and a prompt to save modified profiles. The DAW now adopts the committed `6b53056` equalizer implementation with exact source/data hashes in [reuse provenance](../reuse/equipment/provenance.json). The source checkouts remain untouched; later equalizer changes need a new reviewed snapshot and migration/acceptance checks.

## Current desktop workflow

Build with `SC_BUILD_DESKTOP=ON`, then open **Equipment → Profile library and editor…**.
This works without opening a project. The library has 1,087 generated speaker-model
corrections across 255 brands from the pinned equalizer catalog; model/measurement
conditions and source attribution are visible. It is model-level generated EQ, not
individual-unit or room calibration. There are no invented amplifier or microphone
measurements. The microphone/amplifier source registry is retained as research metadata.

- Filter by kind, brand and family; search model, variant/serial notes and measurement conditions.
- Import equalizer schema 2 JSON or relative response text (`TXT`, `CSV`, `FRD`, `CAL`),
  create a profile, edit PK/LS/HS filters, and export JSON. A third text phase column is
  explicitly ignored. Already-inverted gains belong in JSON; absolute SPL needs normalization.
- The orange curve shows supplied measured response only within its covered display range.
  Teal shows the correction's magnitude at 48 kHz. Generated catalog entries generally
  contain correction filters, not measured response arrays. There is no target-curve editor yet.
- Add/remove up to 16 filters; edit frequency/gain/Q or drag a control point. Undo/redo
  has a bounded 256-state local history; one mouse drag is grouped. Metadata joins that
  history. This is separate from canonical session/parameter undo.
- Save always creates a custom identity with parent provenance, preserving the reference.
  Modified-window close, Escape and Cancel ask Save/Discard/Cancel. Cancel retains the draft;
  Discard writes nothing. Save/Cancel stay outside the scroll area. Unfocused wheel input
  does not change filters or selection.
- The DAW's personal library is in its own Qt application config directory (`equipment.json`),
  independent of both equalizer libraries. Atomic `QSaveFile` replacement validates all entries,
  refuses duplicate identities and bounds the library to 256 profiles / 16 MiB.

Schema 2 exchanges speaker/microphone/amplifier profiles with the pinned equalizer.
**DAW schema 3** additionally names `whole_system`; older equalizers will reject that
version explicitly. It represents the combined microphone/amplifier/speaker/room route,
not an isolated component response. All JSON imports reject duplicate keys, unknown fields/filter types
and wrong types/versions, rather than silently losing data. Limits: 1 MiB/profile, JSON
depth 32, 4,096 response points, increasing finite frequencies 10–40,000 Hz and ±200 dB;
filters 20–20,000 Hz, ±6 dB and Q 0.1–6. Import reads are capped even if a file grows.

Fitting reuses bounded coordinate descent on relative magnitude at 48 kHz, omits positive
filters below 80 Hz and fits only supplied coverage. It does not reconstruct phase or
claim exact inversion. There is no live sweep/measurement capture in this editor yet.

### Boundaries and outstanding gates

This component is an **offline Qt GUI adapter**, with response-only C++ coefficient
helpers. It neither changes a track's current EQ nor applies profiles to monitor routes.
No profiles are embedded into the canonical session yet. The engine still has no Qt
linkage. The future shared engine profile schema/preparation layer, control-room/cue
routing, explicit printing, project pins/relink, target curves and native measurement
workflow remain required X005 work. File/library I/O and fitting currently run on the
GUI thread with size/work bounds; worker ownership and responsiveness qualification
remain open. Nothing runs in the real-time callback.

The copied Spinorama generated-EQ catalog is pinned to
[`acc757bb`](https://github.com/pierreaubert/spinorama/tree/acc757bb98d63327092ee537bde25d9c227811f3),
whose [README](https://raw.githubusercontent.com/pierreaubert/spinorama/acc757bb98d63327092ee537bde25d9c227811f3/README.md)
labels the project GPLv3 and whose [license](https://raw.githubusercontent.com/pierreaubert/spinorama/acc757bb98d63327092ee537bde25d9c227811f3/LICENSE)
is retained. Adaptations clamp gains/Q and omit positive sub-80 Hz filters, so these are
not exact copies of upstream corrections or a quality-equivalence claim. Original plots,
articles and manufacturer calibration files are not included. Per-source redistribution
and corresponding-source review remain a release gate. The Pyle hand-plotted electrical
transcription is **not bundled in this DAW** pending its independent data-rights review.

Linux offscreen editor/menu workflows, immutable references, numeric/import failures,
fit sign/quality, all catalog entries and bounded save/reopen checks are recorded in
[the X005 evidence](../tests/results/X005/2026-10-05-equipment-editor.json).
Native Windows compilation/execution and localized/native-review/accessibility/HiDPI
qualification remain open. Translation contexts exist for principal labels/errors, but
this is not a delivered European-language feature. No frozen parity row is marked done.

## Required completed-product workflows

- Browse profiles by equipment kind, brand, family, model and measurement variant. Preserve source URL, author/date, license, measurement conditions, calibrated versus uncalibrated status, serial-specific microphone calibration, confidence and valid frequency range.
- Import owned/redistributable profiles and measured response/calibration curves. Support the equalizer exchange schema where verified; report unsupported filters/fields and schema versions rather than silently dropping them. Bounds, numeric validation, channel/rate checks and file/parser failure tests are required.
- Show measured/reference response separately from editable correction and the chosen target response. Let users adjust filters/curves, audition and undo/redo. Keep published references immutable; editing creates a custom derivative with provenance. Prompt Save/Save copy/Discard/Cancel when switching or closing a dirty editor.
- Include microphone, speaker, amplifier/receiver and measured whole-system profiles. An acoustic room curve measured through other speakers cannot be reclassified as an amplifier-only electrical response. Load/impedance, gain/tone settings, measurement angle, microphone calibration and room conditions remain attached to the source.
- Select monitor-system profiles independently for control-room outputs/cue destinations. Keep monitor correction out of raw takes and normal exports. Input monitoring/measurement calibration also stays separate from recorded raw media by default. Printing an input or output correction requires an explicit route/render choice with a project-visible audit trail.
- Store stable profile IDs/schema versions and content hashes in portable projects, with missing-profile placeholders and relink/validation. Global libraries and project-pinned copies have separate lifetimes; updating a library must not silently change a saved project or render.
- Share framework-independent profile/curve/fitting data and processors across Linux and Windows. The editor, importer, errors, prompts and help join the all-Europe localization audit; English-only or empty catalogs do not meet that requirement.
- Discover published measurements broadly through a source registry with reproducible acquisition and per-source rights/conditions. Published visibility is not redistribution permission. Keep metadata/reference links when reuse rights are unresolved; do not invent missing model curves or claim equipment coverage from names alone.

## Staged tasks and acceptance

| Task | Dependencies | Required exit evidence |
|---|---|---|
| X005-A data and provenance | Pinned snapshot adopted; complete shared schema and data-rights review still required | Versioned shared schema; exact source hashes; attribution/license inventory; reference versus derivative distinction; malformed/bounded import and round-trip fixtures |
| X005-B library and editor | X005-A, session undo/state and Qt UI | Brand/family/model/variant browse; measured/correction/target plot; editable filters; undo/redo; dirty-switch/close save-copy/discard/cancel workflows; Unicode/localized controls and window-fit qualification |
| X005-C processing and routing | X005-A, M4 control-room/cues and prepared graph | Profile/rate/layout admission and bounded smooth live changes; monitoring correction changes captured monitor signal but leaves raw takes/default exports bit-identical; explicit print choices tested independently |
| X005-D catalog and portability | A/B/C, project portability/import architecture | Audited source registry and rights; model/condition coverage report; project-pinned profile survives relocation/library upgrade; Linux/Windows native workflows and missing-profile recovery |

Do not combine stacked microphone, speaker and amplifier inversions without a declared signal/measurement domain and checked headroom/valid-band policy. Poor or narrowband measurements must have visible uncertainty; deep acoustic nulls and out-of-band extrapolation cannot justify unbounded boosts. Define processing-quality acceptance with a known electrical/acoustic corpus before claiming correction quality.

The microphone/amplifier hand-plotted PDF remains an equalizer research candidate; this DAW records source metadata only, not its independently unverified arrays or a distributable asset. No proprietary content is assumed reusable.
