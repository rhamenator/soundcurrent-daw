# Restoration package profile reader

The shared `tools/restoration_worker_images.py` reader checks a collected helper
profile before a future package builder uses its payload plan. The caller supplies
an independently qualified collection SHA-256, expected source commit and native
platform. Hashing an unchecked collection and passing that hash is not independent
qualification. The reader does not execute helpers or grant package admission.

For example, on the native platform:

```sh
python3 tools/restoration_worker_images.py --verify-profile /path/to/collection \
  --expected-source "$qualified_source_commit" \
  --collection-sha256 "$qualified_collection_sha256"
```

The v2 collection manifest binds the byte count and SHA-256 of every other file
in the exact inventory, including the controller command, full stdout/stderr and
captured input list. Historical v1 collections remain historical evidence and
are not accepted by this reader. Verification requires pinned manifest bytes,
profile schema and serialization, four platform-specific helper images and their
source/native identities, captured build context, installed-controller proof and
repository source archive consistent with the independently qualified captured
input list. The reader does not reopen Git or recompute the source tree; the
caller’s qualification must establish that relationship and the CMake cache hash.
Duplicate JSON keys, changed payload paths,
qualification flags, extra/missing files and linked entries are refused. Recorded
controller commands are inspected as evidence; their old absolute paths are not
executed or reopened.

The reader binds the parsed manifest and profile to the same bytes it hashes. It
also audits the same compressed source bytes whose hash it checks. Metadata reads
are limited to 2 MiB, controller output to 1 MiB, compressed source to 128 MiB,
each source file to 64 MiB and the total decompressed tar stream to 256 MiB,
including PAX/GNU metadata, padding
and trailing bytes before any tar parser consumes them. Existing
image limits remain 256 MiB each and 512 MiB total. These checks do not provide an
atomic filesystem snapshot: a future builder must verify the actual copied
payload before admission and preserve the qualified evidence.

The producer now writes `collection.pending.json`, invokes the same read-only
reader, and publishes `collection.json` only after successful readback. Its local
self-derived pin checks internal consistency; independent exact-head native
execution and external artifact qualification remain required. A failed readback
leaves an incomplete pending collection and never publishes a final manifest.

Synthetic mutation tests cover the contract only. Exact-head hosted Linux and
native Windows collector executions must exercise
this publication step using actual installed controller evidence before merge,
with final manifests retained and no pending manifests. Verification receipts
explicitly report no new native
execution and no package admission. Default preview inventories, dependency source
bundles, standalone qualification, installers and installed GUI qualification
remain separate gates; this change produces no release.
