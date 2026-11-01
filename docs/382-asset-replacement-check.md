# Exact asset replacement verification

This is the first control/I/O implementation step for P084 missing-media relink.
It verifies an explicitly selected candidate against an existing asset without
changing project state or publishing a replacement file. It is not yet a desktop
relink workflow or a claim of complete P084 acceptance.

`verifyAssetReplacement` reads through an `ApprovedMediaFile`, validates every
sample using the existing bounded WAVE validator, and requires the saved
whole-file SHA-256, frame count, sample rate and channel count to match. A matching
filename, matching PCM with different container bytes, or matching duration alone
is insufficient. Malformed expected identities fail before decoding. Existing
format restrictions, source identity checks, cancellation, resource accounting and
finite-sample checks remain in force. Floating-point headroom is preserved.

The developer probe exposes this check with a separate
`sc-asset-replacement-check-v1` protocol:

```text
sc-approved-wave-probe --match-asset --root ABSOLUTE_SELECTED_DIRECTORY \
  --relative PORTABLE_SELECTED_REFERENCE --maximum-bytes BYTE_LIMIT \
  --asset-sha256 SAVED_SHA256 --asset-frames SAVED_FRAMES \
  --asset-rate SAVED_SAMPLE_RATE --asset-channels SAVED_CHANNEL_COUNT
```

Only a completed exact match emits `matchesAsset: true`. Refusal emits no success
output and returns a bounded error record. Neither saved paths nor probe output
grant authority to open another path. The approved handle must remain pinned and
be reverified during any subsequent copy/publication. This API is for a serialized
I/O owner, never the audio callback; decoder execution still requires a supervised
child and deadline before desktop use.

The existing native C++ WAVE corpus exercises exact matching across PCM, float,
RIFF/RIFX/RF64 and extensible cases. Additional cases cover mismatched digests and
format identity, malformed expected metadata, cancellation, resource refusal and
unchanged source/asset state. The real-child probe test independently hashes
Unicode-named float audio with headroom and checks success/refusal protocols.

Remaining P084 work includes a bounded desktop missing-media review, explicit
candidate selection tied to project epoch/revision/asset ID, supervised verification,
exclusive verified restoration/publication, ambiguous/multiple candidate handling,
and native Linux/Windows recovery/save/reopen acceptance preserving clips, takes,
provenance and Undo/Redo. No approximate matching is enabled by this change.
