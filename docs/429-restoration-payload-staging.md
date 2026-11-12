# Restoration helper payload candidates

The shared restoration collector can now stage a helper-only package candidate
with `--stage-output`. Linux uses `payload/usr/bin/<helper>`; Windows uses flat
`payload/<helper>.exe` files. This selects the same four roles from the verified
profile. Existing preview payload inventories remain separate.

For an already collected profile, supply the independently qualified manifest pin:

```sh
python3 tools/restoration_worker_images.py --verify-profile /path/to/collection \
  --expected-source "$qualified_source_commit" \
  --collection-sha256 "$qualified_collection_sha256" \
  --stage-output /path/to/new-candidate
```

The destination must be new, have an existing plain parent, and lie outside the
input collection. Existing candidates are never overwritten. Every copy is
streamed within its pinned byte extent and checked against its SHA-256, including
an independent destination readback. The complete 17-file collection is copied
under `provenance/`, including the repository source archive, profile, input list,
images and original controller proof. That copied collection is verified again;
the four payload files receive final hash/extent checks before publication.
Linux payload helpers receive mode 0755. Windows payload helpers retain their
native `.exe` names; copying them does not execute them.

A failed copy or readback leaves `candidate.pending.json` and never publishes
`candidate.json`. Success has exactly 22 files: 17 provenance files, four payload
files and the final receipt. Linked, extra, missing or non-plain entries are
refused. The receipt binds the source commit, platform, collection/profile hashes
and staged payload paths/bytes/hashes. This is a checked copy, not an atomic
filesystem snapshot; a future package builder must verify its actual admitted
bytes and preserve provenance.

CI exercises the same staging implementation after the owning Linux shard and
Windows main suite collect actual native images. Their producer-derived pins
establish candidate consistency only. Independent exact-head native execution,
retained-original integrity and external semantic qualification remain mandatory
before merge. Synthetic ELF/PE fixtures test both layout contracts without
qualifying native execution.

All admission, dependency source, standalone bundle, installer and installed-GUI
flags remain false. The source archive includes the captured repository license
and source; it does not establish dependency-source bundle qualification. These
candidates do not install helpers, change a delivered preview, qualify an
installation lifecycle or promote a release.
