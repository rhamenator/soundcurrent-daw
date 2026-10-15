# Paired history review and admitted WAVE decoding

Date: 2026-10-14. Baseline SC-DAW-BASELINE-2026-10-05 remains unchanged.
The full DAW goal and all broader quality/native/localization gates remain open.

## Implementation

Commit `5b67777f60dcf0296e07244c7067ab97ec4969b9` shares a completed historical
review within one paired operation. Both current source snapshots are checked
before work and again before return. Sharing requires every exact historical
record to be covered by a completed review, including its expanded unchanged
siblings. Different histories receive separate full reviews. Publication,
verification, span workers and adoption use this helper, with retained resource
leases and dependency guards. Every operation starts a fresh review; there is
no persistent file-trust cache. See [ADR 183](decisions/183-paired-history-review-sharing.md).

The first Debug experiment still failed. A bounded partial Callgrind profile
identified byte decoding and repeated span access as a concrete bottleneck.
Commit `2de1710a9efbd1463dbafb883041bd6c86639a0d` uses unaligned-safe `memcpy`
loads for admitted native-order 16/32/64-bit values and reuses admitted RF64
buffer views. Other byte orders and widths retain byte accumulation. File
hashing, extent/format/finite/peak checks, resource and I/O limits, observation,
cancellation and deadlines remain. No resampling or DSP sample algorithm changed.
See [ADR 184](decisions/184-admitted-rf64-decoding.md).

## Source-bound results

Both complete qualifying runs used source `2de1710a9efbd1463dbafb883041bd6c86639a0d`.
Their source and executable hashes remained unchanged throughout.

| Configuration | Result | Elapsed | Scope |
| --- | --- | --- | --- |
| Initial Debug at `5b67777` | 3 failed | 519.35 s | Paired history and v2 worker suites exceeded 180 s; repeated production later reached an unchanged child deadline |
| Corrected Debug at `2de1710` | 4 passed | 629.67 s | Paired history 134.33 s; v2 workers 150.07 s; repeated production 345.23 s; approved WAVE 0.02 s |
| Release at `2de1710` | 12 passed | 366.86 s | Original history, paired history, v2 workers, repeated production, desktop Apply, source, handle plans, permission, partition, session, WAVE and long-path suites |

One successful Debug repeated worker took **9.9911 seconds of its 10-second
limit**. This is little timing margin, not portable performance qualification.
No deadline was increased. Fresh original Linux and native Windows CI are required.

The new paired-history corpus uses three source rates. It covers both orders,
stale sources before and during review, cancellation, memory/sample limits,
covered-output corruption and corruption between calls while a prior result is
retained. Three additional real original-assembly fixtures prove that disjoint
histories are verified independently, even with shared raw media. The reviewer
itself renders no audio; these fixtures do produce new original audio.

| Source rate | Separate complete reviews | Shared complete review | Compared PCM samples, separate → shared |
| --- | --- | --- | --- |
| 44,100 Hz | 1.454 s | 0.706 s | 142,872 → 71,436 |
| 48,000 Hz | 0.855 s | 0.418 s | 196,816 → 98,408 |
| 96,000 Hz | 1.207 s | 0.602 s | 65,744 → 32,872 |

These Release timings describe this machine and run; correctness is not gated
on a wall-clock speed ratio. The original historical suite retains 531 checks,
186 refusals and 405,432 PCM comparisons. Nine v2 workers and four required
failures retain 350,902 worker comparisons. Repeated production performs nine
actual adoptions with fifteen workers and three retained assembled mixed cases,
502,422 unchanged outer-playback samples and 508,182 live/export samples. All
nine repeated export hashes exactly match checkpoint 294, and all three legacy
exports retain their prior hashes. Observed children are reaped and resources
released. Shared-renderer comparisons do not establish independent sound quality.

The existing approved-WAVE corpus passes 20,956 checks, including independent
exact RF64 PCM8/16/24/32 and Float32/64 values, headroom, malformed/truncated
containers, opposite-endian headers, NaN/Infinity, cancellation and limits.

## Preserved evidence and failed experiments

Evidence is in [the checkpoint directory](../tests/results/M2/2026-10-14-comp-history-sharing/).
Each ZIP stores original file bytes by hash with a `files.json` name mapping;
JSON indexes identify source, run roots, sizes and digests. All payload hashes and
ZIP CRCs were checked. Earlier evidence is unchanged.

| Archive | Bytes | SHA-256 |
| --- | --- | --- |
| `acceptance.zip` | 14,046,215 | `e933bd0e36210505d4cc234aef082a00de720e82e05aaad03369609fdc2fe82b` |
| `initial-debug.zip` | 8,081,947 | `f9382860b259a62c5315fe1bf8453a4a5f393162ebb759ea2873c22abc918a32` |
| `debug.zip` | 12,024,939 | `6f80e7da51429fbeff87bc2859cc75b5cbc96bc063252b170bb15b028097ab9f` |

The failed Debug corpora, original stdout/stderr and partial profiler output are
retained. The profile stopped after 75.22 seconds with the original assembly
deadline refusal under instrumentation. Its 13,852,789,990 captured instruction
references are a partial prefix, not a successful review or production timing.
Byte/span work accounted for 44.21% of self instructions in that prefix.
An earlier focused fixture failed on a duplicate UUID because a pending job
still reserved retained outputs; the fixture now removes that pending reference
before combining independently adopted histories. Its failed and corrected logs
are retained; full qualification above supersedes that smoke test.

After the source-bound runs, two test fixture stream helpers were changed to use
the existing Windows extended-path adapter. This is a test-only path correction
for deep historical fixture files, distinct from the tested production changes.
Seven translation units pass MinGW syntax checking; the receipt records exact
final source hashes. Native runtime
confirmation remains due. Linux uses the adapter's identity path. These edits
are not presented as part of the preceding source-bound execution.

## Delivery boundary and next implementation

The application still refuses repeated rendered-transition creation until its
actual repeated create/review/Apply workflow is qualified. The core workflows
above do not lift that gate. Next, exercise repeated and mixed retained-source
edits through the main window with Apply/Undo/Redo/save/reopen/export, freshness,
cancellation and close handling, then remove only the qualified gate. Preserve
current-source localization checks for any GUI edits.

Original CI run 38655828613 was terminal and fully retained/audited before this
head advance: all six full logs/APIs, six original ZIPs and 26 screenshots. Its
failures and path evidence are recorded in checkpoint 294. New CI must confirm
the Windows path corrections and workflow timing. Before another head advance,
retain and audit its original terminal evidence as well.

No installed application, audio route, VM, equalizer repository or release changes
are included. No production UI text changed. Broader processing quality,
nonuniform/protected production, native installers and audio, all-European
localization, arbitrary track/layout qualification, and M2–M11/F/Q/C/N/X004/X005
remain incomplete.
