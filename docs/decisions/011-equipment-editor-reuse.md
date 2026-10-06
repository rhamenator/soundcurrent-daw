# ADR 011: adopt a pinned offline equipment editor

Status: accepted for the preview, 2026-10-05; full X005 remains open.

The owner requires equipment profiling and an editor from the equalizer projects.
Those checkouts now have committed implementations. Copy reviewed equalizer revision
`6b53056`, preserve notices/hashes and adapt it in the DAW repository. No branch or
working-tree edits are made to the equalizers. The private Studio implementation is
related code, not an implicit shared library/API dependency.

Expose library/import/filter-curve editing/custom saves in the Qt desktop menu.
Keep it outside the Qt-free live/offline engine; the reused GUI model is provisional.
Do not wire it to the track EQ as a substitute for independent control-room correction.
M4 must implement monitor/cue separation and explicit printing with raw/default-export
invariance tests, using a framework-independent prepared profile model.

Retain the generated speaker EQ catalog and original collector/license/report for
traceability. Import microphone/amplifier/whole-system measurements supplied by the
user; bundled measurement coverage is not implied for those kinds. Pyle electrical
arrays await their data-rights audit. Schema 2 preserves equalizer common-kind exchange;
DAW schema 3 declares the extra whole-system kind, so old importers reject it explicitly.

Add strict fields/types/versions/bounds, separate atomic DAW storage, immutable reference
copies, local undo/redo and focused-only wheel handling. Screen-fit scrolling keeps
save/close choices accessible. Principal UI strings use Qt translation contexts; actual
all-Europe translations/native review remain required. Bounded GUI I/O/fitting is a
preview limitation; worker ownership is a later responsiveness gate.

Record current acceptance evidence and gaps in docs/17-equipment-profiles.md and X005
results. Catalog breadth and parser/editor tests establish no correction quality,
Windows qualification, native-project compatibility or reference-product parity.
