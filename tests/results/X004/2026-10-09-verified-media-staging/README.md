# Verified staging evidence

Local Release/sanitized and compile-only cross logs belong to checkpoint125.
`receipt.json` hashes source/evidence; the decoder assertion failure stays separate.

The `import-media-final-*` files and `import-media-merged.json` are the earlier
PR68 final passing cohort: source a99576e5a7c97cabab05e0f7e97ecafc798226c0,
merge f783684e06e6b53f0a48c96dc95e6c58cc35450c, run37896975094. Verified original
metadata/ZIP digests and extracted LastTest logs are retained byte-for-byte.
Their original `.cache/` hash-map paths map to same-basename files in this folder.
They qualify the prior GUI/checker, not this new staging implementation.
No product binaries, credentials, VM or user recording data are included.

The first native staging cohort (e94c1ce/run37899638411) fails its first-copy
process handle oracle after9 checks while actual worker61 passes. The verified
original core archive/job log and follow-up primed-oracle logs are separate.
`warm-oracle-receipt.json` hashes the changed test and retained failure evidence;
it does not relabel the initial receipt/source hashes or assert native completion.
