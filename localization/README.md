# Language work inventory

`language-register.json` contains 130 initial language/script work items plus regional/script candidates. All are planned; no GUI catalogs are available yet. The register is extensible. Its size must never be described as the number of translated languages or as exhaustive European language coverage.

Family entries such as Sámi, Mari and Romani require disambiguating specific community languages/varieties before catalog work. A family tag is not an umbrella translation that can be marked complete for every member. The initial European scope includes trans-European communities and languages of territories linked to European countries; it makes no political classification claim.

`research/language-tag-validation.json` records identifier checks against the IANA register and the downloaded source hash. All current tags/variant subtags are registered; this does not verify Qt/CLDR coverage, translation quality or locale formatting.

Read [the localization contract](../docs/08-platforms-and-localization.md) before adding catalogs. User-facing strings will first appear in S6. Keep translation, native review and UI qualification status separate; record the language/script actually reviewed. Cross-border/regional differences can share text only after reviewers approve that choice.
