# Bounded European language inventory audit

Date: 2026-10-06. Requirement: X002. Scope: planning data, not translated support.

The desktop now exists, but the language inventory still had the initial 130
work items. This audit checks for omissions using the
[Council of Europe table](https://rm.coe.int/november-2022-revised-table-languages-covered-english-/1680a8fef4),
whose document status date is **9 December 2025**, and the
[IANA language subtag register](https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry),
File-Date **2026-09-17**, SHA-256
`755fad43283be7b41ebe3c89ad054b6eaf928f404f9c0edb74799e0eab74beb1`.
The source URL's older filename is not its document status date.

The table helps identify omissions. Its treaty coverage does not define the
product's entire European scope. Country, community, variety and script coverage
outside that table still needs an independent audit. IANA verifies identifiers;
it does not settle which communities can share a reviewed DAW translation.

## New planned work

| Inventory identifier | Work item |
| --- | --- |
| `aii` | Assyrian Neo-Aramaic; community/orthography mapping needs review |
| `ku` | Kurdish umbrella; individual catalogs still required |
| `kmr` | Northern Kurdish; script/community review required |
| `lad` | Ladino; separate script review |
| `pap` | Papiamento; regional orthography review |
| `fkv` | Kven Finnish |
| `fit` | Meänkieli |
| `frp` | Francoprovençal / Arpitan |
| `yec` | Yenish; source-name mapping needs review |
| `fax` | Fala |
| `ext` | Extremaduran |
| `ary` | Moroccan Arabic; candidate mapping for the named Darija community |
| `acy` | Cypriot Arabic; candidate community/script mapping |

This makes **143 planned language work items**. Add registered variant candidates
`sco-ulster` and `oc-aranes` to their existing parents. These candidates are not
approved shared catalogs or delivered variants. Do not substitute a broad Arabic,
Finnish or Kurdish catalog merely because a language belongs to that grouping.

Four separate mapping work items keep Bunjevac, Boyash, Leonese and Yezidi visible
until community/identifier decisions are reviewed. Their base-language candidates
are research leads, not approved aliases or automatic fallbacks. The pinned IANA
register has no `leonese` variant. A missing registered subtag is not permission
to remove the community from scope or count another catalog as its translation.

## Verification and remaining delivery

All original 130 identities and qualification states remain unchanged; two
existing variant lists grow. Every language/script/region/variant candidate is
registered, with applicable variant prefixes checked. There are no deprecated
preferred-value aliases in these work items. The four unresolved community IDs
are stable work-item identifiers, not invented BCP 47 language tags.

Every catalog state remains planned; native review and UI qualification remain
false. No empty catalogs, translations, new fonts, CLDR bundles, runtime dependency
or language picker are added. English source UI operation does not qualify the
planned multilingual workflows. Identifier validity cannot prove Qt formatting,
plurals, scripts, bidirectional behavior, terminology or accessibility.

Evidence lives in
[the coverage audit](../research/europe-language-coverage-audit-2026-10-06.json) and
[identifier validation](../research/language-tag-validation.json). The source
register bytes were used for validation but are not bundled with the application.

This independent planning-data change happened while native diagnostic handle
5818 was confirmed live. The inventory is not an input to that binary: all 62
pinned native source files, binaries and processing libraries remain unchanged.
No related engine source/build change or CPU benchmark occurred during the run.
Its later failure and retained audio require their own evidence; this audit does
not resolve the recording gate. No equalizer writes or push/publication.

Next X002 work: expand community/member-language/script/accessibility coverage,
audit actual desktop source strings and extract meaningful Qt catalogs. Implement
language selection independently from formatting locale and qualify real reviewed
catalogs on Linux and Windows, including errors, recovery, help and installers.
Non-written-language/accessibility strategies need review as part of the wider
coverage audit; no comprehensive all-Europe claim is made. Full frozen DAW parity,
native platform/quality gates, X004/X005 and the complete goal remain required.
