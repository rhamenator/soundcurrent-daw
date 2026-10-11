# Explicit monitor channel mapping

2026-10-11. M2 engine progress. The full frozen suite, Linux/Windows parity,
installation and European localization remain open.

## Scope

Prepared monitoring now admits an explicit sparse channel matrix after causal
rate conversion and before output-bank publication. The converter and startup
retention still use the original input width. Raw fanout samples, stamps, rates
and frame counts are unchanged. No native adapter, project-clock alignment or
normal UI factory is activated by this change.

The named mono-to-stereo helper explicitly copies unity amplitude to both output
slots. Stereo downmix, channel permutation, polarity/gain and unrouted silent
slots use declared source/destination/gain entries. No pan law, loudness
normalization or speaker-position assignment is inferred. Existing absent-map
same-width processing stays on its original identity path.

Reuse: the repository's GPL ChannelMix value type and validation conventions
(zero-based slots, finite gains up to magnitude64, unique pairs) are reused.
PreparedMixGraph is not instantiated merely to map a monitor block: it owns
track EQ, events and planar mixing state unnecessary for this interleaved route.
No external dependency or equalizer repository change is introduced.

## Real-time and numeric contract

One processing owner, immutable admitted routes, zero added frame latency/tail.
Construction/destruction and ledger acquisition remain outside RT guards, after
owners join. Counts are bounded:1–256 input/output slots,1–65,536 routes and a
prepared frame maximum. The processing group fixes that maximum at its existing
64-frame grid. Work grows with routes times frames; this limit is not a
worst-case hardware deadline qualification.

The mapper accumulates in double precision in declaration order, checks every
input (including unused slots) and every output before copying any destination
sample. Finite float headroom is preserved without clipping. Invalid geometry,
nonfinite input and float-range overflow refuse the whole block and leave the
destination unchanged. Overlapping input/output spans are supported through the
prepared accumulator. Explicit maps do not promise preservation of signed-zero
bits after arithmetic; absent-map identity and raw samples retain their old path.

The processing group separately charges the retained declaration, prepared
matrix, accumulator and mapped pending block. Output pressure retains the exact
mapped PCM and pauses further input/controller/converter progress. A map failure
terminates only monitoring, preserves its first cause and leaves original raw
recording data available to the writer.

## Version boundary and remaining gap

The native auxiliary1.1 journal has no declared channel matrix. This slice
therefore refuses explicit maps combined with startup admission, before claiming
owners or accepting frames. It does not silently store mismatched-channel
readiness as same-layout1.1 state. Project/raw schemas are unchanged.

Next: add a versioned native declaration and original mapping provenance, with
explicit migration/legacy refusal, then compose mono-input/stereo-output full-FIFO
startup, SDK warmup/stop and save/recovery workflows. Only after that gate can
an owned native recording factory expose the route to the ordinary UI. Native
physical audio, installed workflows, recording alignment and deadline guarantees
remain unqualified.

## Acceptance and exact source

Frozen sourceaecc0b79934a89a428e914e61ee20eb294538f85, tree
dabb2958bae1dab828a3264396165a4e117a8156. Ordinary inheritance of protected
MAIN143c76a preserved the entire candidate tree. Original documentation-conflict
stages and resolution are retained. No source implementation changed during
qualification.

| Profile | Selected suites | Total test time | Processing checks | Guarded allocate/free/lock |
| --- | ---: | ---: | ---: | ---: |
| Release | 4 | 1.69s | 498,462 | 0 |
| ASan/UBSan | 4 | 17.15s | 498,462 | 0 |
| TSan | 4 | 26.35s | 498,462 | 0 |

The suites are bounded monitor processing, bounded startup, composed native
startup/persistence and original native outcomes/migration. Tests cover literal
sparse/permutation/full65,536-route results, overlap and output canaries, invalid
geometry/nonfinite/duplicate/range/resource refusal, mono-to-stereo,
stereo-to-mono and sparse monitor composition against independent converter
output. Backpressure preserves exact mapped bytes; every original raw sample
compares bitwise. Numeric overflow withholds the entire mapped grid, preserves
the first fault and leaves the raw route intact.

The first overflow fixture stalled without an explicit finite end. The failed
original/debug logs are retained; the corrected fixture supplies the existing
end contract and drains output before assessing overflow. No production
invariant or deadline was relaxed.

The [qualification packet](../tests/results/M2/2026-10-11-monitor-channel-map/summary.json)
binds636 Git/checkout inputs,12 Linux executable hashes, nine Linux objects,
three Windows objects, three Windows executables and the native SDK archive.
Commands, configurations and full original logs are preserved with original and
compressed hashes. Unchanged mixing-source/system-header warnings are retained;
no warning suppression or false-positive diagnosis is claimed.

The Windows SDK adapter and three fixtures compile/link; the three changed
Windows objects compile without warnings. These new Windows binaries have not
been executed locally. Physical audio, hardware deadlines, actual native startup
mapping and ordinary UI activation remain outside this qualification. No product
executable, object or media is included in the public evidence packet.
