# Translation review — 2026-10-07

## Scope and evidence

A contextual AI review examined the 30 starter strings in each of 32 unverified languages (960 translated entries). This is an internal review, not an independent review or native-speaker certification. No external translator service was used. Remaining untranslated strings were not certified by this review.

Corrections address clear semantic risks and ambiguous audio terminology: Swahili gain previously used a word for profit and bands a word for belts; frequency-band labels were made explicit in several languages; flat-response labels/reset actions were clarified; Hungarian regional settings and Vietnamese audio/preset labels were improved. These replacement translations remain unverified themselves.

`data/localization/translation-context.json` supplies technical definitions in generated Qt Linguist translator notes. Gain means a signed signal-level adjustment in dB; flat means zero EQ gain, not mute; balance means left/right channel level; Quit exits the process while closing the window leaves it running. Notes are shared between the applications and can be reused by the DAW.

## Remaining uncertainty

All 32 language packs remain incomplete and lack native review. Particular review priorities are Swahili audio vocabulary, Thai preset wording, natural microphone EQ terminology in every language, and regional Portuguese and Chinese usage. An AI reread or back-translation by the same model is not independent evidence and must not be labeled native review. Do not promote catalog review status based on this pass.

## Required review workflow

1. Review the English source and actual control behavior with the translator notes. Resolve ambiguous source wording before translating it.
2. A fluent reviewer should compare each target string directly with that intent and flag wrong meaning, unintended connotations, register, regional usage, spelling and unnatural phrasing. Back-translation can help identify questions, but does not replace this comparison.
3. Check strings in the running interface, including RTL, truncation, mnemonics, decimal entry, and complete error messages. Verify Quit, reset and lock semantics explicitly.
4. Record reviewer identity/locale, date, catalog commit, corrections and unresolved issues before setting nativeReviewed. Any later edited strings require renewed review.
5. Keep uncertain entries unfinished so they fall back to English. Keep partial-language coverage visible. Full language qualification also requires the release gates in localization.md.

## Automated checks

Catalog audit checks source inventory, duplicate keys, nonempty finished entries, exact placeholders, literal `&&` preservation, absence of hidden bidi override/isolate controls, coverage and compiled-catalog hashes. The interface supplies RTL direction explicitly. These checks verify structural integrity, not translation accuracy or cultural appropriateness.
