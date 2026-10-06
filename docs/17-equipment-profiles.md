# X005 equipment profiling and profile editor

Owner requirement added 2026-10-05; [machine-readable inventory](../research/equipment-profile-requirement.json). **Required, not yet implemented in the DAW.** This supplements the frozen combined-reference scope; it does not modify that baseline or turn equipment coverage into a parity claim.

The owner is adding equipment profiles/import and a curve editor in the separate **SoundCurrent equalizer development** chat. Its current scope includes speaker and microphone imports, amplifier references, broad published-measurement discovery, brand/family/model browsing, visible response curves, adjustable correction and a prompt to save modified profiles. Treat that ongoing work as a reuse candidate; do not edit those checkouts here or copy mutable files while their implementation is being changed. Adopt a reviewable snapshot with exact revision/hashes, GPL notices, source/data rights, adapted interfaces and acceptance tests once suitable.

## DAW workflows

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
| X005-A data and provenance | Reviewable EQ profile snapshot; dependency/data license review | Versioned shared schema; exact source hashes; attribution/license inventory; reference versus derivative distinction; malformed/bounded import and round-trip fixtures |
| X005-B library and editor | X005-A, session undo/state and Qt UI | Brand/family/model/variant browse; measured/correction/target plot; editable filters; undo/redo; dirty-switch/close save-copy/discard/cancel workflows; Unicode/localized controls and window-fit qualification |
| X005-C processing and routing | X005-A, M4 control-room/cues and prepared graph | Profile/rate/layout admission and bounded smooth live changes; monitoring correction changes captured monitor signal but leaves raw takes/default exports bit-identical; explicit print choices tested independently |
| X005-D catalog and portability | A/B/C, project portability/import architecture | Audited source registry and rights; model/condition coverage report; project-pinned profile survives relocation/library upgrade; Linux/Windows native workflows and missing-profile recovery |

Do not combine stacked microphone, speaker and amplifier inversions without a declared signal/measurement domain and checked headroom/valid-band policy. Poor or narrowband measurements must have visible uncertainty; deep acoustic nulls and out-of-band extrapolation cannot justify unbounded boosts. Define processing-quality acceptance with a known electrical/acoustic corpus before claiming correction quality.

The microphone/amplifier hand-plotted PDF is being assessed in the equalizer chat; this DAW requirement records that candidate, not an independently verified measurement or a distributable asset. No proprietary content is assumed reusable.
