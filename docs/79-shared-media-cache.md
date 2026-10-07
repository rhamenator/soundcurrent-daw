# Shared media handles and decoded-page cache

X006 checkpoint, 2026-10-07. A mix now owns one resource-admitted media registry,
handle pool and decoded-page cache shared by its track readers. The same readers
serve live playback preparation and offline rendering. This removes the former
per-track file duplication and asset-inventory ceilings. Full track scalability,
sustained real-time capacity and frozen-reference parity remain incomplete.

## Ownership and data flow

The control/preparation owner validates an immutable session and indexes assets
and tracks by stable IDs. MixReader collects only assets whose clips intersect
the prepared range. It charges the existing graph/pipe/read-buffer payload, clip
bindings and shared cache against the configured graph budget before constructing
readers. A standalone TrackReader charges its pipe/EQ/read buffers and cache
against its playback budget. A shared-reader caller owns aggregate admission.
Project state stays schema 1.7; neither file handles nor cache policy is persisted.

One serialized disk owner accesses the cache, verifies sources, decodes pages and
fills the existing bounded slabs in fair prepared track order. Audio callbacks
consume those slabs through the existing shared clock and prepared EQ/matrix.
Hashing, allocation, file IO, cache eviction and destruction stay outside the
callback. Cache statistics are disk-owner data, not safe concurrent GUI/RT reads.
The cache cannot be shared across independently running read workers without a
new ownership protocol. Retirement still cancels/joins the worker off RT.

Pages are keyed by asset ordinal and aligned source frame, rather than project
track or clip. Independent clip starts/source offsets retain their exact frame
coordinates. A cached page may serve many tracks and repeated clips. Float32
pages retain raw nonfinite values; each rendered occurrence is sanitized and
counted by TrackReader, then clips accumulate in float64 before slab publication.
The cache has no amplitude limiter, gain normalization or hidden resampling.

## Resource and integrity contract

MediaCacheConfig defaults to 64 concurrent files, 4,096 frames per page, 8 MiB
page payload and 16 MiB registry/work payload. Page geometry uses the largest
required asset channel layout; mixed layouts may waste space. Page storage and
handle slots are prepared before publication. No fixed total asset or track count
is imposed: the trusted registry/graph budgets decide admission. File handles are
closed before replacement, with LRU reuse. Decoded pages also use bounded LRU
slots and an index; index node allocation occurs on the disk owner.

All required sources are verified before publication. Each reopening checks plain
owned paths and hashes the exact descriptor subsequently passed to libsndfile,
then verifies WAV/WAVEX/RF64 rate, channels and extent. Changed or unsafe sources
are refused. Matching source rate is still required. Hash cancellation remains
available during both preparation and reopen. Cache hits reuse admitted bytes:
concurrent asset rewriting during an admitted generation is outside the immutable
owned-filesystem contract. This is not a continuous hostile-writer snapshot.

Charges cover declared owned payload and conservative work allowances, including
one 64 KiB hashing scratch and file-library allowances. They do not establish an
allocator-overhead, libsndfile-internal memory or process-RSS hard bound. Combined
old/new graphs, validation staging, retained history, GUI and other process memory
still need measured aggregate envelopes. Reopening can rehash an entire large
file; heavy eviction may miss disk deadlines. Scheduling/prefetch improvements
and sustained profiles must measure this cost rather than infer it from track count.

## Developer API migration

Legacy names remain source-compatible, but their meaning changes:

- ReadAheadOptions.maximumOpenAssets and MixReader/MixPlaybackRun's final
  maximumOpenAssetReferences argument cap **concurrent shared handles**. The
  effective cap is the minimum of those and cache.maximumOpenFiles.
- ExportSettings.maximumOpenAssetsPerTrack/maximumOpenAssetReferences use those
  same concurrent-handle policies. They do not reject a larger asset inventory.
  ExportSettings.mediaCache exposes trusted registry/page/handle policy; it is
  charged inside the overall export memory budget and used by both export APIs.
- MixReader.openAssetReferences() returns size_t and counts **unique admitted
  assets**, not duplicated track references or currently open descriptors. Use
  mediaStatistics().openFiles/peakOpenFiles on the serialized owner for handles.

No external stable ABI or persisted policy is promised for this development API.
An application that used the old names as inventory limits must supply an explicit
trusted registry/state budget instead. This checkpoint adds no dependency:
existing C++20, libsndfile and OpenSSL/Linux or BCrypt/Windows implement the pool.

## Bounded file-backed acceptance

The retained test projects contain generated float WAVs with Unicode filenames,
known sample patterns and independent source-coordinate/matrix output oracles.
These are headless synthetic file-backed workflows, not native device runs.

| Workload | Output compared | Peak files | Decoded pages / hits | Exact difference |
|---|---:|---:|---:|---:|
| 257 tracks, one shared asset, 8,192 frames | 16,384 samples | 1 | 3 / 8,444 | 0 |
| 1,024 tracks, one shared asset, 8,192 frames | 16,384 samples | 1 | 3 / 33,937 | 0 |
| 96 tracks / 96 distinct assets, 768 frames | 1,536 samples | 2 | 288 / 0 | 0 |

The shared-asset cache charges 1,127,680 bytes; the distinct-asset cache charges
232,632 bytes. The latter has 384 verified opens and 382 evictions, illustrating
hash work under a small handle pool. Linux /proc/self/fd checks the actual owned
media descriptor count during operation and zero after destruction. These figures
are workload evidence, not capacities or performance promises.

Other cases cover split page reads and source offsets, one-page eviction,
repeated cache hits, typed extent/budget refusal, hash cancellation, changed-file
and symlink refusal on reopen, descriptor cleanup, repeated nonfinite clips, and
zero callback allocations/frees/blocking locks. Save/reopen keeps IDs/routes/assets
and exact output. All generated projects remain retained even after failures.

The initial existing mix test failed its old duplicated-reference-count assertion;
the new semantics correctly report one shared asset. Its original source,
executable hashes and log are retained. That fixture's old automatic cleanup
removed its generated project before failure reporting, so original project/media
bytes are unavailable. Subsequent fixtures do not fill this original evidence gap.
The initial regex also failed to select playback-read-ahead; later runs select it
explicitly. No native observation is added and original sanitizer observation 71's
clock gap/source CPU cause remains unresolved.

See the [dated receipt](../tests/results/M2/2026-10-07-shared-media-cache.json)
for frozen sources, original failure, logs and final qualification. Next implement
virtualized track/timeline/meter views, measured combined memory and desktop
resource controls, then large recording/adoption, freeze/bounce and sustained
Linux/Windows profiles. All 92 frozen F/Q/C/N contracts, X004 imports, X005 equipment,
all-Europe delivery and independent Windows UI/native/install qualification remain.


## Export policy review correction

Review found that export exposed its overall memory but not the default16MiB
cache registry policy. ExportSettings.mediaCache now configures the shared reader
inside aggregate export memory. A dedicated real API case reproduces the original
ignored1-byte registry policy for both track/mix export, retaining original source,
executable hashes, log, project/media and wrongly published destinations. The fixed
case refuses both before publication, then exports exact reference samples with
a32MiB registry policy and unchanged saved state. This is policy propagation, not
an above-default asset-inventory stress measurement.

The [review receipt](../tests/results/M2/2026-10-07-shared-media-export-policy.json)
records full Debug45/45,127.81s, affected export ASan/UBSan/LSan2/2,5.11s, and fresh
Windows media/export/test compile/link. Earlier seven sanitizer groups retain
their initial checkpoint source scope. No native/Windows runtime/sustained or
frozen parity claim follows.
