# ADR 079: Context-mapped drafts and display-only numeric normalization

Date: 2026-10-08. Status: accepted for the desktop preview.

Adapt reviewed GPL equalizer localization updates inside the DAW. Preserve exact
input snapshots, immutable original adaptation pins and the DAW's real Linguist
contexts/numerus keys. Use explicit context/source vocabulary mappings, not a
global translation lookup. Preserve pending translator work during regeneration.
Track translation count, native review and UI qualification separately.

Normalize only formatter direction marks at numeric input; isolate number/unit
pairs only at display. Stable IDs, project data, raw takes and processing do not
depend on interface language. Embedded standard-action translation has explicit
runtime ownership and retirement. Prefer QLocale script/territory resolution over
a growing hardcoded list while refusing incompatible explicit variants.

No new framework or dependency. Drafts remain partial, plurals/diagnostics/help
and installer coverage remain open. See [checkpoint 100](../100-localization-reuse-refresh.md).
