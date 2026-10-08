# ADR072: contextual desktop catalogs and independent format preferences

Status: accepted for the bounded localization foundation, 2026-10-07.

Adopt reviewed GPL equalizer localization patterns in a Qt-only DAW adapter.
Use real Qt Linguist context extraction and committed embedded TS/QM resources,
with maintainer-only generation tools and dependency-free structural checks.
Keep interface language distinct from formatting locale, project IDs/serialization
and processing. Save preferences for restart; retain explicit fallback policy and
accurate draft/completeness/native-review/UI status.

The equalizers' single-context regex extraction cannot correctly identify the
DAW's multiple C++ runtime contexts. Explicit Qt context declarations and NOOP
markers make extraction and runtime agree. Runtime translator/locale/direction
ownership stays outside the engine and audio callbacks. Numerical timelines/plots
keep their direction under RTL application layout.

Costs and open gates: only eight draft contextual keys per non-English catalog,
untranslated plurals/diagnostics/help/setup, no native speaker or native Windows
qualification, and exact Qt minimum-version compatibility. No new toolkit,
project schema, DSP algorithm or frozen parity promotion. See [87](../87-desktop-localization.md).

