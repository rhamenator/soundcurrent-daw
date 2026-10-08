# Reviewed localization and equipment-editor refresh

The full frozen suite goal remains active. This is an incremental desktop preview
improvement, not European language coverage or platform parity.

## Exact upstream inputs and adaptations

Read-only review froze public EQ `6081fd4a25d19b8fd15121e67c5852f9af1f1ac5`
and Studio `a6d152b2b29530123fe517f02d2cfbc3db8cdd3f`. Eleven registered
inputs changed in each repository. Retained GPL snapshots now also include each
repository's 33 non-English TS catalogs. The additive review inventory has 110
inputs; immutable original DSP/equipment provenance and snapshots are unchanged.
`reuse/upstream-review.json` records each exact content hash and source revision.
No equalizer branch, source or existing untracked cache was modified.

- Numeric widgets strip only formatter presentation marks ALM/LRM/RLM while
  retaining signs, digits, separators and cursor position. Arabic, Persian and
  Hebrew signed integer/fractional input is exercised through real spin boxes.
  Existing held-step acceleration and focused-wheel policy remain.
- Catalog selection uses Qt language/script/territory resolution, including
  Traditional Chinese on Taiwan, distinct Portuguese regions and Norwegian
  Nynorsk. Explicit incompatible scripts/unavailable regions fall back to English.
  Unicode/private formatting extensions do not select a different catalog.
- Standard Qt dialog actions use the embedded application catalog. Translator
  retirement restores the prior locale/direction; OS-native file dialogs retain
  their OS language. No external Qt translation directory is required.
- Signed numerical meter values and invariant dBFS units are isolated at the RTL
  display boundary. Project state, catalog text and audio remain unchanged.
- Stable equipment kind data and field limits were already adapted. Source and
  conditions fields now have stable widget identifiers as well. This does not
  apply equipment correction to audio or complete profile portability.

The upstream single-context extraction is not imported. Real Qt Linguist context,
disambiguation and numerus keys remain authoritative. Draft vocabulary is mapped
only to reviewed identical source/context pairs, recorded in
`reuse/reviews/2026-10-08-localization/context-adoption.json` and
`localization/reviewed-words.json`. Numerus entries are not filled from scalar
equalizer terms. Existing translator text, unfinished plural forms and translator
comments survive updates. Structural checks preserve placeholder multiplicity,
rich tags/attributes/links, file filters, literal ampersands and bidi boundaries.

## Coverage and evidence

There are 564 extracted keys, English plus 33 draft catalogs. English has 559
finished scalar entries; its five plural messages still use Qt source fallback.
Each draft has 95 finished entries (3,135 total), with English fallback for the
rest. No language is promoted to native-reviewed or fully UI-qualified. These
34 catalogs do not satisfy the planned 143-item European inventory.

Six focused Linux CTest groups pass: desktop localization, catalog structural
checks, catalog failures/preservation, equipment editor, desktop export UI and
main desktop UI. Thirteen altered-catalog cases are refused; an actual
lupdate/lrelease cycle retains unfinished scalar/plural work and comments in an
owned copy. Decimal edit/save/reopen/render checks preserve identical samples
across interface/format choices. Native MSVC/Qt tests pass 275 localization checks, equipment editing and export
in limited interactive session 1. The first broader native run passed 7/8: a
recording-controller wait timed out at line 463. Four unchanged rerun tests pass,
including that controller. Both runs and dispatcher timeouts are retained; the
recording timeout cause remains unisolated, so this is not sustained scheduling
qualification. These UI tests use synthetic audio endpoints.

[Receipt](../tests/results/X005/2026-10-08-localization-refresh.json) and
[capsule](../tests/results/X005/2026-10-08-localization-refresh.zip) retain native
logs/input hashes, original failures and Linux/audit evidence. No executable,
credential helper or screenshot is included.

## Remaining work

Translate remaining controls, plurals, diagnostics, help and installer text;
obtain independent native-speaker review and per-platform UI/font/accessibility
qualification. Engine/backend diagnostic messages need their own stable error
boundary, rather than translating saved identifiers. Full European coverage,
equipment routing/portable pins and the Windows installer remain open.

Qt's [QLocale](https://doc.qt.io/qt-6/qlocale.html),
[QTranslator](https://doc.qt.io/qt-6/qtranslator.html) and
[QAbstractSpinBox](https://doc.qt.io/qt-6/qabstractspinbox.html) documentation
describes the toolkit boundaries. These changes add no dependency and do not
raise the existing Qt API floor.
