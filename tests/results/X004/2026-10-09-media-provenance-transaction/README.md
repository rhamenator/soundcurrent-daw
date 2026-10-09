# Media provenance transaction evidence

`provenance-*` logs are local current checkpoint 126 cohorts. Initial diagnostics,
test-fixture correction, scoped gates, abrupt interruption and final known-writer
cases are retained separately. `receipt.json` hashes source and evidence.

`staging-final-*` and `staging-merged.json` belong to the prior final passing PR 69:
source 0b5e1291171b772d3c3eddf9350e457214a35393, merge 937f83b57a0c889c09f5d7faafa58f552a30fdc8,
run 37900548308. Original metadata/ZIP digests/extracted logs remain exact; they
qualify the earlier primitive, not the new publication/recovery code. Their
hash-map names map directly to same-basename files here. Native warm-up retained
one process-global handle (86 -> 87); exact subsequent +3 and retirement checks
pass. The source of that first persistent handle is not separately attributed.
No product binaries, credentials, user recordings or VM disk data are included.
