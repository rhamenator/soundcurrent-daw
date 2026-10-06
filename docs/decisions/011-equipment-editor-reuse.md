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

## Catalog/taxonomy update, 2026-10-05

Adopt the later committed equalizer equipment update `459627c`, retaining per-file
revisions and SHA-256 hashes in the provenance manifest. Five equipment source/data
files advance; unchanged mathematical references, original tests, source registry and
license stay pinned to `6b53056`. Do not include unrelated equalizer effect/guard code.
The collector remains an archived acquisition reference, not a runtime network task.

The catalog adds five admissible models (1,092 total across 255 brands), leaving all
prior correction filters unchanged. Schema 2/3 optional subtype/power fields retain
the equalizer exchange names and bounded text validation. Missing fields use explicit
unclassified/unknown defaults without changing identity, filters or response arrays.
Editor metadata uses the existing local undo/custom-copy contract. Library subtype and
power filters combine with kind/brand/family/search; translated visible kind labels
use stable data identifiers. No catalog entry count establishes correction quality or
microphone/amplifier measurement coverage. Source/data release gates remain open.
