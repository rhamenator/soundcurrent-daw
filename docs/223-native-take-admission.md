# Native originals on the project timeline

2026-10-12. The full suite goal remains active and incomplete.

## Usable workflow

Open the project, select the destination track and open its saved native take in
**Native recording**. Set **Start frame (project rate)**, then choose **Add to
selected track**. The inactive worker verifies the original WAV and native receipt
files before one transactional history command adds the asset and clip. Undo removes
the placement; Redo restores it. Saving/reopening preserves the placement and
provenance. Success releases the retained take view without deleting its files.

The track layout must match the original. The original stays at its native sample
rate with floating headroom and source frame zero. Existing mixed-rate playback
and export use the source/project frame map; the clip duration is expressed at the
project rate. Placement is explicitly chosen by the user. This does not infer
recording alignment from native capture ordinals, monitor counters or GUI time.
Automatic alignment still needs a verified transport clock and latency mapping.
Monitoring EQ is not printed into the take.

## Model and ownership

Schema 1.18 adds bounded `nativeEvidence` records to assets. Each record retains a
portable path, byte count and SHA-256 for the original `native-run.json`,
`journal.json`, `packets.bin` and any declared monitor map/EQ files. Older schemas
migrate to empty native evidence. Derived stretch assets clear this lineage; the
original remains referenced through the existing raw anchor. Project media
verification checks the original WAV and these evidence files. A parsed model
alone does not establish verified media.

The immutable admission ticket carries a resource lease and is accepted only by
the originating project controller/ledger with the matching project, root and
structural guard. Raw UI structural commands cannot supply an unverified native
asset. Repeated placements reuse an identical asset; an ID collision with different
content is refused. Owned file handles pin the original and receipts during
verification; receipt hashes are checked again before adoption. Active, foreign,
nonfinite, incomplete, mutated or over-budget sources are refused. This uses the
existing owner-controlled project filesystem contract, not a hostile-filesystem
sandbox. GUI/real-time callbacks perform no verification I/O.

## Current qualification

Implementation `01d76a109625e0027fbc779bc0db42b837196e3f` binds 788 source/checkout
inputs. All 144 local Release tests pass in 175.35 seconds. The durable original
suite reports 1,728 checks and the actual Qt panel reports 93 checks. The fresh-window
case has no SDK factory, enters frame 12345 through the real control, clicks Add,
checks the native asset/receipt hashes, performs Undo/Redo, saves/reopens and checks
that all original files remain present. Core cases cover 44.1/48/96 kHz projects,
repeated placement, conflicting assets, corrupt receipts, negative placement and
quota refusal.

Four core/state ASan/UBSan tests pass. The first GUI invocation fails during GTK
platform-theme initialization: the test-only allocation interposer's delete/free
meets a GTK nothrow-new allocation. The original report is retained. The same GUI
binary passes in 11.42 seconds with `QT_QPA_PLATFORM=offscreen` and an empty platform
theme, with allocation-mismatch checking still enabled. Leak checking is disabled
for this qualification; GUI leak/TSan and permanent framework ownership remain
open. This is scoped evidence, not a clean physical desktop sanitizer claim.

Structural localization and 13 corruption/refusal workflows pass: 34 catalogs,
984 source keys and 3,168 draft translations; no language review/UI qualification
is promoted. Eleven current Windows objects compile without warnings, including admission,
session edits/store, native control and the genuine WASAPI adapter. Current
Windows link/runtime qualification is pending CI. See the
[qualification packet](../tests/results/M2/2026-10-12-native-take-admission/summary.json). Compilation alone does not qualify Windows runtime or an
installed/physical audio route.

## Preceding CI and retained gaps

PR157/run38213151977, which predates timeline admission, has three successful jobs:
Windows desktop 20 tests/84.82 seconds, native 71/264.38 seconds and core cross-build.
Its Linux job builds all 493 steps, passes tests 1–11 and is canceled near the
20-minute job envelope while test 12 is running. The logs do not prove a test-12
failure. The current Linux job envelope is 30 minutes; per-test limits are unchanged.
All four original job logs and selected original Windows runtime artifact members
are retained. No unchanged retry was used to replace this result.

Native Linux monitoring stream integration, automatic recording alignment, richer
take browsing/recovery, current Windows runtime/installers/physical routing and
all later milestones remain required. This checkpoint does not establish frozen
workflow parity, bundled-content equivalence or completed European localization.

Next implementation: connect the native monitoring controller to an owned Linux
PipeWire stream adapter, using the existing daemon and the established bounded
packet interface. Qualify synthetic owned routes before audible testing.
