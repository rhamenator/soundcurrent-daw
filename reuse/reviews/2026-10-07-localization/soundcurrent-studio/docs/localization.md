# Localization

EQ and Studio share the same Qt translation/runtime interface. It is reusable by the DAW without coupling the DSP to Qt. Copperfin was inspected read-only: its deterministic fallback, pseudo localization, placeholder checks and separation of machine contracts inform this design. No Copperfin or DAW files were modified.

## Current coverage

English is the source language. **32 other catalogs are partial, unverifieds, not complete or native-reviewed language packs.** Core controls have unverified translations; detailed instructions/errors and unreviewed terms fall back to English. `data/localization/catalogs.json` records exact message counts and review status for each app. A catalog is never called complete just because it loads.

Coverage is global: European languages, Arabic, Hebrew, Persian, simplified/traditional Chinese, Japanese, Korean, Hindi, Indonesian, Vietnamese, Thai and Swahili. Portuguese for Portugal and Brazil and the two Chinese scripts are separate catalogs. Regional fallback reuses a base-language catalog only where one is explicitly available; explicit script variants are not collapsed into another script. The prior 143-item Europe inventory is retained as **planned** work in `language-inventory.json`, including minority languages. The inventory is extensible and is not a claim of full global coverage.

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

Edit `data/localization/soundcurrent_TAG.ts` with Qt Linguist. The shared context is `SoundCurrent`; user-facing literals use `SC_TR(...)`, or `soundcurrent::i18n::text(...)` at a data-driven view boundary. Keep `%1`, `%2`, `%n` and `%L1` placeholders intact. Use full sentences with placeholders instead of concatenated translated fragments. Current UI messages are not numerus messages; extend the maintenance script to preserve Qt TS numerus entries and add plural regression tests when plural sentences are introduced.

```sh
python3 scripts/localization.py --update
python3 scripts/localization.py --check
```

Updating requires Qt Linguist `lrelease` (Qt 6 preferred). Compiled QM files are committed with SHA-256 provenance in `catalogs.json` and embedded in every Linux/Windows build; ordinary builds need only Qt Core/Widgets. The audit requires only Python and checks extraction freshness, blanks, duplicate keys, placeholders, coverage and TS/QM hashes. Add a locale and its native name to `seed-translations.json` before the first update; subsequent updates preserve existing translator edits. Use proper language/script/region tags; do not copy a regional label to imply a reviewed regional translation.

Developer-only test languages `qps-ploc` and `qps-rtl` expand text and exercise RTL layout. `--localization-ui-test --language TAG` constructs the real views without starting audio/network processing. `--ui-self-test` remains deterministic English regardless of the user's preferences.

Before qualifying a language: translate **all** messages, obtain native-speaker review of audio terminology and regional usage, check dialogs/accessibility/mnemonics, decimal input, Unicode paths, RTL channel/frequency ordering, 1280×720 and HiDPI layouts on both Linux and Windows. Installer text and native driver/vendor interfaces are separate translation work; 1.1.0 installers remain English. The native driver and system routing contracts stay unchanged.

References: [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html), [QTranslator](https://doc.qt.io/qt-6/qtranslator.html).

## Translation accuracy

See [translation review](translation-review.md) for contextual corrections, known uncertainties and fluent-review requirements. Audio terminology notes in `data/localization/translation-context.json` are embedded in Qt Linguist catalogs. Structural audits do not certify linguistic accuracy.
