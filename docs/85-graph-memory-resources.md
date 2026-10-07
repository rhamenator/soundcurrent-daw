# Shared prepared graph and media resources

X006 checkpoint, 2026-10-07. This extends the trusted controller/GUI parent from
[84](84-gui-memory-resources.md); it does not establish full scalability or parity.

Current desktop policy and added recording/receipt leases are described in
[86](86-execution-memory-policy.md); the local-policy limitations below describe
this earlier checkpoint.

## Ownership and preparation

Core execution options accept an optional caller-owned `ResourceLedger`. Empty
options preserve standalone local payload policies. The desktop injects the
project controller's parent into playback, fixed-range recording, manual recording
and offline export. Project files cannot configure or raise that trusted parent.
The Project resources dialog reports its combined declared usage.

| Owner | Declared leased payload | Lifetime |
|---|---|---|
| PreparedMixGraph | DSP, routing, mix scratch and nested EQ drivers | Prepared generation through control retirement |
| MixPlayback | The graph above, per-lane playback pools, planar scratch and replacement mask | Whole playback generation; nested graph is not charged again |
| MixReader | Range clip bindings and each reader's float/double decode buffers, reader vector/metadata | Readers destroyed off RT |
| TrackReader (standalone) | Its own bindings, decoding buffers and metadata | Last reader owner |
| MediaReadCache | Registry, handle allowances, all admitted pages and serialized hash scratch | Last shared cache borrower |
| PlaybackRun | Playback pipe, nested EQ/driver and processor planar scratch | Reader canceled/joined, then payload destruction |
| AudioBridge | Monitoring EQ/driver and bridge metadata | Quiescent off-RT destruction; external capture pool excluded |
| WAV export | Private mix/readers/cache plus two output buffers | Worker unwinds after failure/cancel/publication |

Local mix/reader/export payload ceilings still apply independently of shared parent
credit. Playback/export retain their default 128 MiB local processing allowance;
raising the project parent alone does not raise every local owner policy. Core
callers can configure those policies; a coordinated desktop graph/IO policy
editor remains work. Explicit child scopes may use separate limits while sharing a root.
Read-ahead and graph options inherit the other owner's parent when unspecified;
cache options inherit the reader parent when unspecified. An explicit cache child
is preserved in read-ahead APIs. Export uses the trusted ExportOptions parent
in preference to the render specification cache setting. A shared cache holds one lease, regardless of reader count.

Payload reservations happen before owned DSP/page/reader buffer allocation or
media verification. Constructor errors unwind reservations. Validation indices,
asset-inventory trial vectors, caller-owned plans/options and filesystem destination
inspection can precede admission; this is not whole-process heap admission.

Leases are declared before owned payload members so credit releases after payload
destruction. They survive the caller's ledger facade. No reserve, resize, release,
ledger mutex, reader cancellation/join, media IO or destruction runs in callbacks.
Graph replacement keeps both generations charged until the audio owner signals
its last use and the control owner collects retirement. Desktop Stop/Prepare retains
its existing stop-first behavior; it does not implement seamless graph replacement.

## Acceptance and remaining work

`graph-resources` exercises 512-track prepared graph overlap/refusal/retirement,
parent cache refusal before hashing, shared last-owner release, cancellation and
IO-constructor rollback, mixed reader rollback/retry, exact live/offline floating
point samples above full scale, export refusal without destination publication,
concurrent live/offline ownership, single/mixed reader cancel/join, and monitoring
DSP admission. Existing event/smoothing/timing/RT and persistence tests remain
required. Desktop adapters are qualified independently of native hardware.
The checkpoint receipt records exact executed scopes and retained failures.

These are conservative declared payload weights, not exact allocator/RSS, library
internal allocation, all descriptor/process overhead or measured CPU admission.
Capture pools/writers, parser/other IO buffers, expanding edit trials/command payloads,
Qt overhead, paging, remaining meter/waveform work and multi-worker scheduling remain
open. Recording retains its separately visible 256-arm/packed-input implementation
bound. Freeze/bounce, sustained modest/strong Linux and Windows workloads and all
frozen-reference/native Windows/localization/import/profile gates remain required.
No arbitrary hardware throughput or universal low-latency track count is claimed.
