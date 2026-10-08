# Desktop localization foundation

X002/X003/X005 checkpoint, 2026-10-07. Status: bounded Linux fixtures qualified.
This is a partial catalog/runtime foundation, not all-Europe language support.

## Product behavior

**Settings → Language and regional settings…** selects the interface language
independently of number/date formatting. Save remembers the preferences; Cancel
discards the dialog changes. Restart applies them after normal project shutdown.
The dialog reports the requested/resolved catalog, actual translated message count,
English fallback and the need for native-speaker review. It does not reconstruct
active views or touch an audio callback when settings are saved.

Startup precedence is `--language TAG`, saved non-system language, the optional
`SOUNDCURRENT_DAW_LANGUAGE` environment variable, system UI languages, then English.
Only embedded resources are loaded. Tags are bounded and validated; file paths are
not language selectors. Exact region/script catalogs remain separate. Approved
application preview fallbacks cover German DE/AT/CH, French FR/BE/CH/CA, and the
explicit Chinese Hant-TW/Hans-CN variants; unapproved scripts fall back to English.
These preview fallbacks are not a native terminology certification.

Preferences use the DAW application's own QSettings scope. Project serialization,
IDs, route intent, sample values and user names are language-independent. The
runtime owns/removes its translator and restores the previous default locale and
layout direction when destroyed. Timeline time and equipment-chart frequency
continue left to right under an RTL interface; user text remains Unicode.

## Catalogs and maintenance

Qt's actual `lupdate` extraction identifies context/source/comment/numerus keys.
Classes use `Q_DECLARE_TR_FUNCTIONS` matching runtime contexts; data-driven captions
are marked with `QT_TRANSLATE_NOOP`. This corrects model and nested equipment-editor
context mismatches rather than assuming a regex extractor discovers C++ scope.
See [Qt's extraction documentation](https://doc.qt.io/qt-6/linguist-lupdate.html)
and [source translation conventions](https://doc.qt.io/qt-6/i18n-source-translation.html).

`python3 tools/localization.py --update` needs Qt 6 Linguist maintainer tools.
`--check`, ordinary desktop builds and CI use retained TS/QM resources without
requiring translator tools. The source inventory records exact UI input hashes
and the generator versions. Translator entries and plural forms are preserved;
unfinished entries are excluded from QM compilation. The structural audit checks
contexts, coverage counts, hashes, placeholders, literal ampersands and hidden
bidi controls. Any UI source change requires regeneration.

There are **33** nonempty catalogs: English source plus **32 partial, unverified
drafts** reused from the equalizers. Each draft translates **8 of 543** contextual
keys; the aggregate 256 counts context/message entries, not 256 unique meanings or
complete languages. English has 538 finished entries; five existing numerus source
messages remain unfinished and use source fallback. Native-reviewed and fully
UI-qualified languages remain **zero**. Developer expanded and RTL pseudo-locales
are test tools and do not count as translated languages.

The Europe register remains **143 planned work items**, with unresolved community
identifiers and further coverage work. The 33 catalogs also include non-European
languages inherited from the upstream seeds; they do not replace that register.
No frozen functional/quality/content/native-project row is promoted.

## Reviewed reuse

Read-only committed sources: public EQ `2f0a576e4ab752ec5c9343bbc39f09960845e143`,
premium EQ `41c5655ce9f3d2703eacc3a8248559df0a375ac5`. Exact inputs/notices and the
previous inventory are retained in [the review directory](../reuse/reviews/2026-10-07-localization/).
The current inventory registers 44 inputs and preserves original DSP/editor
adaptation provenance. No equalizer working tree is edited.

Adaptations: DAW resource/settings namespace; actual Linguist extraction instead
of single-context regex extraction; explicit script-preserving fallback; runtime
lifetime restoration; DAW-context exact-word draft seeds; installer-independent
embedded catalogs; numerical plot and timeline direction; isolated project/render
tests. Upstream candidate improvements include contextual extraction, lifetime
restoration and regression workflows. They are not applied back to the equalizers.

## Acceptance and gaps

The owned offscreen fixture loads every embedded catalog, checks real window/editor
contexts, regional/script fallback and preference precedence, edits an actual EQ
control with a decimal comma, saves/reopens a Unicode project, compares offline
audio sample hashes across locales, and compares actual equipment-chart pixels
under English/RTL. It uses fake audio endpoints and never activates native audio.
Retain source/executable hashes, fixture projects, WAVs and screenshots in evidence.

The [original receipt](../tests/results/X002/2026-10-07-desktop-localization.json) records
full Linux Debug **61/61, 157.49s** and affected ASan/UBSan/LSan
**6/6, 31.32s**, with matching source/resource input hashes. Ten isolated catalog
corruption cases are refused. Original fixture compile/context/prompt errors and
the prompt timeout are retained separately; no pre-fix product failure is claimed.

Subsequent hosted Linux qualification on Qt **6.4.2** loads the embedded catalogs
and passes all **58/58** non-native tests. The packaging correction's local Debug
cohort passes **62/62**; the changed Python/version and desktop checks pass **3/3**.
Compiled C++/Qt resource hashes match the earlier passing affected sanitizer cohort.
See the [separate runtime receipt](../tests/results/X007/2026-10-08-preview-runtime.json).
This scopes minimum-Qt catalog loading; it does not qualify complete translated
workflows or native Windows.

Open: all remaining translations and native review; correct translated numerus
coverage; engine/worker diagnostics, startup CLI help, help/recovery and installer
text; Qt standard-button/dialog translation packs; mnemonic/font/accessibility,
1280×720/HiDPI/full RTL workflows; native Windows Qt/runtime;
per-language recording/recovery/export qualification. See
[X007 installation](88-easy-installation.md). A catalog-load or pseudo-locale test
does not qualify a language, device backend or platform.
