# Localization

EQ and Studio share the same Qt translation/runtime interface. It is reusable by the DAW without coupling the DSP to Qt. Copperfin was inspected read-only: its deterministic fallback, pseudo localization, placeholder checks and separation of machine contracts inform this design. No Copperfin or DAW files were modified.

## Current coverage

English is the source language. **All 33 non-English catalogs, including Nynorsk, are populated for the 524 currently extracted source messages. No language pack is native-reviewed.** This is catalog coverage, not proof that every user-facing message has been extracted. Missing interface strings are deferred to the requested second pass. Populated translations remain unverified; missing messages fall back to English. `data/localization/catalogs.json` records exact counts and review status. Contextual AI review, native-speaker verification, compiled runtime checks and visual/package qualification are separate evidence categories.

Coverage is global: European languages, Arabic, Hebrew, Persian, simplified/traditional Chinese, Japanese, Korean, Hindi, Indonesian, Vietnamese, Thai and Swahili. Portuguese for Portugal and Brazil and the two Chinese scripts are separate catalogs. Regional fallback reuses a base-language catalog only where one is explicitly available; explicit script variants are not collapsed into another script. Qt locale data maps region-only Chinese tags (zh-TW/zh-CN) to the corresponding available script catalog and accepts equivalent explicit Latin-script Portuguese tags. Without a Portuguese region, Qt’s default territory selects the regional pack; unsupported explicit regions fall back to English. Unicode/private-use formatting extensions do not block language selection. The prior 143-item Europe inventory is retained as **planned** work in `language-inventory.json`, including minority languages. The inventory is extensible and is not a claim of full global coverage.

The number/date locale selector independently exposes the locales supplied by the installed Qt version (hundreds of language/region/script combinations). Having a formatting locale does **not** mean that the UI is translated into its language. Qt 6.4 and Qt 6.12 may provide different inventories.

## Use

Open **Settings & calibration → Language and regional settings**. Choose the interface language and number/date format independently. Use **Quit** and reopen to apply changes; closing the window leaves the process running. This avoids rebuilding views while live audio is active. Unsupported languages or missing messages use English. Native OS file dialogs may continue using the operating system's language.

Selection precedence: `--language TAG`, saved app preference, `SOUNDCURRENT_LANGUAGE` when the preference is system, then system UI-language preferences, then English. Example:

```sh
soundcurrent-eq --language fr-CA
soundcurrent-studio --language pt-BR
```

Language selection uses embedded resources; it never downloads catalogs or executes translation files. Settings persist stable language tags. Preset IDs, model/brand names, custom names, schema keys, routing IDs, frequency limits, units and JSON numbers stay invariant. Built-in preset display names can be translated while their item data retains the original ID. Qt numeric controls parse the selected number locale; JSON/DSP use numeric values. RTL layouts do not reverse the frequency band order or change channel mappings.

## Translators and maintenance

Edit `data/localization/soundcurrent_TAG.ts` with Qt Linguist. The shared context is `SoundCurrent`; user-facing literals use `SC_TR(...)`, or `soundcurrent::i18n::text(...)` at a data-driven view boundary. Keep `%1`, `%2`, `%n` and `%L1` placeholders intact. Use full sentences with placeholders instead of concatenated translated fragments. Current UI messages are not numerus messages. The maintenance script rejects numerus entries before rewriting any catalog, until explicit Qt language-specific plural rules and regression tests are implemented. Unfinished translated text and translator comments are preserved during updates.

```sh
python3 scripts/localization.py --update
python3 scripts/localization.py --check
```

Updating requires Qt Linguist `lrelease` (Qt 6 preferred). Compiled QM files are committed with SHA-256 provenance in `catalogs.json` and embedded in every Linux/Windows build; ordinary builds need only Qt Core/Widgets. The audit requires only Python and checks extraction freshness, blanks, duplicate keys, placeholders, literal ampersands, rich-text link/tag integrity, file-dialog extension patterns, embedded resource inventory, coverage and TS/QM hashes. `--require-complete` rejects any unfinished translation across all languages. `requiredCompleteLocales` in the seed inventory prevents already-populated languages from silently acquiring blanks when UI text changes. Add a locale and its native name to `seed-translations.json` before the first update; subsequent updates preserve existing translator edits. Standard Qt action captions use the embedded application catalog; OS-native dialogs may retain the operating system language. Use proper language/script/region tags; do not copy a regional label to imply a reviewed regional translation.

Developer-only test languages `qps-ploc` and `qps-rtl` expand text and exercise RTL layout. `--localization-ui-test --language TAG` constructs the real views without starting audio/network processing. `--ui-self-test` remains deterministic English regardless of the user's preferences.

Before qualifying a language: translate **all** messages, obtain native-speaker review of audio terminology and regional usage, check dialogs/accessibility/mnemonics, decimal input, Unicode paths, RTL channel/frequency ordering, 1280×720 and HiDPI layouts on both Linux and Windows. Installer text and native driver/vendor interfaces are separate translation work; 1.1.0 installers remain English. The native driver and system routing contracts stay unchanged.

References: [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html), [QTranslator](https://doc.qt.io/qt-6/qtranslator.html).

## Translation accuracy

See [translation review](translation-review.md) for contextual corrections, known uncertainties and fluent-review requirements. Audio terminology notes in `data/localization/translation-context.json` are embedded in Qt Linguist catalogs. Structural audits do not certify linguistic accuracy.

Locale inference uses [Qt QLocale language, script and territory data](https://doc.qt.io/qt-6/qlocale.html), rather than treating every four-character suffix as a script.
