# Windows and localization across Europe

Requirements **X001** (Windows functional parity), **X002** (all-Europe localization), **X003** (traceable adaptation of equalizer code) supplement the frozen reference matrix. Baseline versions stay unchanged.

## Platform gates

| Stage | Linux | Windows | Exit evidence |
|---|---|---|---|
| Core/state, now | Native build and tests | MinGW cross-build of identical core and test executable | UTF-8 model/relative media, invariant numeric serialization, stable IDs, validation and save recovery. Native Windows runtime remains unverified. |
| DSP/media, S3/S4 | Shared prepared EQ/capture writer | Same algorithms and test fixtures | Identical offline signal/latency/tail fixtures; native file-failure tests on each OS. |
| Native audio, S5/M2 | PipeWire first, existing JACK later | WASAPI device/format/clock handling; evaluate ASIO SDK/license | Record, monitor, disconnect/reconnect, latency/alignment, quantum changes and interruption recovery on real devices. No system default-route changes. |
| UI, S6 | Qt Widgets and desktop integration | Same Qt views, device adapter and installer | Keyboard/accessibility/HiDPI, localized controls and small-display fixtures on both OSes. |
| Plugins, M6 | VST3/CLAP/LV2 | Native Windows VST3/CLAP; LV2 compatibility explicitly qualified | State/editor/isolation tests per platform/ABI. Linux plugin binaries do not become Windows plugins. |
| Final, M11 | Linux packages and full workflows | Windows packages and same full workflows | Every reference workflow compared independently, cross-OS portable projects, platform-specific gaps visible. Windows release is not inferred from Linux results. |

## Language coverage

“All of Europe” includes countries outside the EU and regional/minority language communities. There is no seven-language cutoff. [language-register.json](../localization/language-register.json) is the initial authored work inventory, including Latin, Cyrillic, Greek and right-to-left scripts. It separates language from region/script variants. It is **not exhaustive**; additions stay in scope. Translation coverage must be audited with contributors across European countries before a complete-coverage claim. Missing CLDR or Qt locale support is an integration gap, not grounds to drop a language.

The [Council of Europe language charter](https://www.coe.int/en/web/european-charter-regional-or-minority-languages/languages-covered) is a useful regional/minority-language coverage cross-check, but its treaty scope does not define our entire product inventory. Language identifiers use [BCP 47's IANA register](https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry). [Unicode CLDR](https://cldr.unicode.org/index/downloads) supplies a reference for locale data; **48.2** is the inspected stable release. Qt's embedded data version must be recorded separately when GUI dependencies are pinned. We do not bundle CLDR data now.

Each locale moves through **planned → translated → native-reviewed → UI-qualified**. These states are independent of whether the toolkit can display its script. Current translations/reviews/UI qualifications: **none**; the current command-line developer tool has English diagnostics. Every empty/missing catalog falls back to source English and is shown as incomplete; it never counts as language support in release notes.

## UI implementation contract

Use [Qt Linguist](https://doc.qt.io/qt-6/qtlinguist-index.html), `QTranslator`, contextual source strings, numerus/plurals, translator comments and `.ts`/`.qm` catalogs. Add catalogs when S6 introduces real UI strings; no placeholder catalogs to inflate coverage.

- User selects language independently of formatting locale; remember the choice. Prefer exact regional/script match, then approved base-language catalog, then English. Do not guess a script from geography or silently substitute an unreviewed variant.
- Display numbers using the chosen locale; parse edited values with the same locale. JSON remains invariant with numeric types. Audio/MIDI/event IDs and routing intent never depend on language.
- Localize menus, transport, track controls, EQ units/descriptions, device errors, missing media/plugins, recovery, export, help and installer text. User track names, file names, plugin-supplied names and third-party interfaces are not translated automatically.
- Test plural forms, decimal comma/point, accented/Cyrillic/Greek names, Unicode file paths and Windows UTF-16 command-line paths. Test bidirectional text without reversing audio-channel order or musical time. RTL layout and user text are distinct concerns.
- Allow at least 40% text expansion, scalable fonts, wrapping and keyboard/mnemonic conflicts; test 1280×720 and HiDPI. Musical symbols need licensed fonts and fallback coverage.
- Translator builds use an expanded pseudo-locale and RTL pseudo-locale before release. Automation and saved projects must remain byte/semantic stable when the UI language changes.
- Native-speaker review includes terminology, errors, accessibility labels and whole workflows, not only a spreadsheet of strings. Language additions must not require engine changes.

## Acceptance traceability

| ID | Workflow | Required evidence | Current gap |
|---|---|---|---|
| X001 | Run the full frozen-reference acceptance corpus on Linux and Windows; move a project between them | Per-OS functional/quality results and portable media/plugin report | Shared state/DSP/capture/media/audio bridge cross-build; no native Windows DAW execution |
| X002 | Select each registered language, record/edit/export, trigger recovery and reopen in another language | Translation completeness, native review, layout/accessibility and invariant project tests for each locale | Registry and Unicode state tests only; GUI/catalogs not implemented |
| X003 | Adapt borrowed DSP, then isolate a candidate improvement for later upstream adoption | Pinned origin/notices/hash manifest, documented divergence and independent fixture results | Audited peaking subset now adapted in the DAW with source snapshots and fixtures; returning changes to equalizers remains later work |
| X004 | Import other suites' native work files and exchange formats | Versioned source corpus, property/render comparisons and persistent preservation/loss reports | Native/exchange adapters pending; see [import contract](10-project-import.md) |

## Next implementation task

S6a: add bounded read-ahead and take playback, then Qt UI and export. S5 owned PipeWire capture/monitor/source-removal fixtures now pass; physical latency/reprepare and native Windows audio remain open. S3 prepared EQ/events/lifetime and S4 capture/disk/journal/recovery foundations are tested; physical filesystem faults, >4 GiB, native Windows runtime and the full graph remain unqualified. Preserve the full DAW, Windows, localization and import scope while delivering the first recording slice.
