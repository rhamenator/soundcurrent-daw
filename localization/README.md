# Language work inventory

`language-register.json` contains 143 planned language/script work items, regional/script candidates and four named communities whose catalog identities remain unresolved. No GUI catalogs are available yet. The register is extensible. Its size must never be described as the number of translated languages or as exhaustive European language coverage.

Family entries such as Sámi, Mari and Romani require disambiguating specific community languages/varieties before catalog work. A family tag is not an umbrella translation that can be marked complete for every member. The initial European scope includes trans-European communities and languages of territories linked to European countries; it makes no political classification claim.

`research/language-tag-validation.json` records identifier checks against the IANA register and the downloaded source hash. All current tags/variant subtags are registered; this does not verify Qt/CLDR coverage, translation quality or locale formatting.

The [2026-10-06 coverage audit](../docs/47-europe-language-inventory-audit.md) adds thirteen work items and two variant candidates. The Council of Europe table is a cross-check for gaps, not the whole product scope. A valid IANA tag does not approve community identity, translation reuse, script choice, Qt support or a fallback catalog. The separate `community_mapping_work_items` keep such choices visible without inventing registered subtags or treating an umbrella language as completed coverage.

Read [the localization contract](../docs/08-platforms-and-localization.md) before adding catalogs. The desktop UI exists; catalog extraction, language selection and per-locale qualifications remain to be implemented. Keep translation, native review and UI qualification status separate; record the language/script actually reviewed. Cross-border/regional differences can share text only after reviewers approve that choice.
