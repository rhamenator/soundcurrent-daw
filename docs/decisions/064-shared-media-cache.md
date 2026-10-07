# ADR064: one bounded media pool per serialized read owner

Status: accepted checkpoint, 2026-10-07; sustained and full X006 qualification open.

Reuse the existing libsndfile decoder and platform hashing/file wrappers. A mix
shares one immutable asset registry, LRU handle pool and source-frame decoded-page
cache across track readers. Charge its declared payload alongside graph/pipe/read
buffers; keep every cache operation on one preparation/disk owner. Audio consumes
existing slabs and runs the same processing path used by offline export.

Do not duplicate handles/cache per project track, impose an asset-inventory cap
through a handle policy, or introduce a second audio framework. Reopening verifies
the same descriptor passed to the decoder; cache hits use immutable admitted bytes.
Legacy developer names now describe concurrent handle caps/unique asset counts;
no project schema change. Existing C++20/libsndfile/OpenSSL/BCrypt dependencies and
licenses remain. Avoid new concurrency/atomic statistics unless a concrete GUI
publication protocol is prepared and tested.

The cost is serialized decoding, conservative maximum-channel pages and possible
whole-file hash work after eviction. The bounded file-backed tests establish exact
source coordinates, descriptor limits and callback separation, not sustained disk
or native Windows capacity. Charges are declared payload, not allocator/RSS hard
bounds. See [implementation and scoped evidence](../79-shared-media-cache.md).


ExportSettings exposes the caller-owned MediaCacheConfig as well as overall
export memory. A default cache policy must not become an inaccessible inventory
limit in an adapter. Both single-track and mix export use the same configured
policy; explicit refusal happens before a temporary/destination is published.
