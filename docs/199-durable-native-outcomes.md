# Durable original native outcomes and recording recovery

Checkpoint199 adds a disk-side native recording transaction around the existing
original raw capture writer. Its current typed payload is for the WASAPI capture/
render adapters. It preserves original SDK outcomes on Linux portable tests and
compiles the Windows paths; it does not activate a device or qualify installed
native recording. The raw journal retains schema1.0 and its existing meaning.

## Transaction and inactive recovery

`NativeOriginalCaptureWriter` admits immutable project/track/asset/run identities,
run generation, actual capture/render descriptors and the initial monitor ordinal.
It writes a pending `native-run.json` before caller activation. The original writer
continues storing RF64 PCM, original packet stamps and their common-prefix digests.
Disk append, checkpoints, serialization, file hashes and publication stay outside
SDK/processing callbacks. Native contexts only produce their existing bounded
in-memory snapshots, which the caller copies after all owners join.

After raw finalization, copied native outcomes are checked and atomically reported
against the verified inactive raw recording. Raw media/journal publication and
native reporting are separate durable boundaries. Interruption between them leaves
saved raw audio plus Pending native state. It cannot claim completed monitoring.
An already reported outcome is immutable; repeat reporting refuses replacement.
No PCM is rewritten or automatically attached to a playable project.

`reportNativeOriginalRecording` also saves joined outcomes against a verified
inactive **partial common raw prefix** after its writer has failed or closed.
This preserves actual native failure/delivery evidence even if raw media never
reached finalization. SDK-delivered frames may exceed the durable prefix; their
original counts remain unchanged. Failed/interrupted monitoring is not converted
to success merely because some audio was saved.

One exclusive inactive job lease covers common-prefix verification, native-state
read and outcome publication. An internal raw-inspection helper permits this
without nested locks; the existing standalone raw inspector still acquires its
own lease. Active writers and missing native lease files refuse native admission.
The bound record includes raw extent, finite prefix, sample/stamp hashes,
finalized state and the **exact raw-journal file hash**. Raw metadata or phase
changes invalidate that binding until an explicit paired migration is implemented.

Old/raw-only jobs return NoNativeEvidence, never monitoring Complete. A caller
expecting a declared transaction refuses an absent declaration. A valid pending
transaction returns its verified raw prefix and no invented SDK end. A reported
missing capture/render owner yields Interrupted with the original end absent.
Known faults/unavailable/native failure take precedence over missing/cancel
classification, while all original facts remain available separately.

## Separate domains and original diagnostic evidence

Capture-generation/request status and original Start/Stop/last packet/callback
counts remain distinct from the stored raw prefix. OwnerLost stores no synthetic
capture end. The first availability HRESULT and original stop request are retained.

The render result preserves its status, own cancel intent, original SDK descriptor,
Start, Finish, observed queue drain, last padding, last content/silence lease,
actual release/Stop HRESULTs, first failure/operation, counter/receipt flags,
callback/native commitment counters, consumed monitor frames, terminal slack and
exclusive monitor ordinal. Original wide unsigned positions/QPC/frequencies and
missing QPC values stay integers. No SDK pointer, fit, hardware/project mapping
or PCM payload is put in this journal.

Complete claims must satisfy descriptor, generation, original end presence,
callback/commitment/slack/ordinal, actual successful drain/Stop and finite Stop
horizon checks. Capture delivery and last packet must match the verified saved raw
evidence. Diagnostic InvalidReceipt/NativeFailure payloads retain original
inconsistent scalar facts instead of normalizing them into a successful receipt.
An observed empty native queue followed by failed Stop remains Failed.

This state classifies the admitted recording/monitoring transaction. It does not
prove acoustic playback, sample alignment, hardware latency, processing quality
or raw playability. Raw finite-prefix/nonfinite evidence remains independent.
Future native backends require explicit payload adapters/version admission; this
WASAPI payload is not a substitute for Linux native outcome qualification.

## Schema, limits and licensing

The auxiliary journal is `soundcurrent-native-original-run` schema1.0 with pending
or reported phase, immutable declaration, bound raw evidence and optional outcome.
The existing raw format/schema and original-only workflows stay separate. Unknown
versions/fields/tokens, duplicate keys, arrays, wrong scalar types and oversized
structure refuse admission. Limits are16KiB journal text, depth12,4,096 parse
events,1,024 keys and64-byte keys. HRESULTs are checked signed32; clock/count
values are checked unsigned64. Enum tokens are stable language-independent strings
with compile-time guards against accidental ordinal reorder. UI translation is a
separate work item; a journal token is not a translated user-facing label.

Native serialization/inspection reserves512KiB workspace credit plus bounded path
cost against the optional ledger. The raw writer/inspector reserve their existing
workspaces/sample pages separately. This is conservative fixed credit, not an
allocation-free JSON parser or a benchmark of every allocator implementation.
Preparation and all serialization/I/O/destruction remain off RT. No new dependency,
driver, GUI route, signing certificate or license change is introduced; code is
original GPL-3.0-only using the existing JSON, hashing and file-publication stack.

## Frozen component and composed evidence

Source `bccfd292cc9d7d6f18f094f83c40b0e6e624898c`, tree
`94381b2810f845a4e99e5afdaf2c7715adffc1c2`, passes ten suites per profile:

| Profile | Seconds | Observed native-outcome checks |
|---|---:|---:|
| Release | 0.69 | 694 |
| ASan/UBSan | 5.14 | 715 |
| ThreadSanitizer | 5.72 | 704 |

Counts vary with bounded process-wait polling. Tests round-trip nine distinct
outcomes at1/2/8/256 channels, original wide clocks/nonzero and negative HRESULTs,
invalid diagnostic fields, failed Stop after drain, missing owners, cancellation,
undrained interruption and consumption-vs-native-commitment differences. They
refuse contradictory Complete/Healthy claims, foreign identities, unknown schema,
duplicate/unknown keys, wrong types, digest mismatch and oversized input. Active
writers and insufficient resource credit are refused without admission.

Four **actual abrupt process exits** occur after pending publication, raw
finalization, before native outcome publication and after outcome publication.
Recovery reads the correct durable prefix and pending/reported outcome without
relying on destructor cleanup. The unchanged raw suite also qualifies its ten
actual process-interruption boundaries. Additional exception-boundary tests and
partial-prefix reporting cover the separate publication path. None is a power-loss
or filesystem crash-consistency experiment.

Thirteen composed capture/processing/disk/render cases now persist and reopen the
production context snapshots: four finite0/7/512/3,072-frame ends, four failed/
missing/cancelled/undrained render ends and five source/nonfinite/failed capture
Stop/cancel cases. A fake SDK owner polls stop even when no complete output block
is available; no unstarted SDK Stop is invented. Independent original disk hashes
and stopped-kernel PCM oracle remain. The fixture passes14,509 checks per profile.
Its64k source, prior fit and SDK pacing are explicitly synthetic; no live startup,
deadline, hardware latency or full EQ/mix execution claim follows.

The [64,739-byte/20-member packet](../tests/results/M2/2026-10-11-native-outcome-recording/summary.json)
binds592 selected Git/checkout inputs,30 Linux executable hashes,15 Windows object/
library hashes, full build/CTest/LastTest logs/configurations/qualification script.
The new native transaction, raw helper and Windows abrupt-exit fixture objects
compile independently. Twelve unchanged SDK objects match their qualified198
hashes exactly; their full core-only build passes with no new compilation needed.
The Windows object checks do not link or execute this new persistence workflow.
Guarded C++/interposed libc allocation/free/blocking-lock audits remain zero for
prepared operations; disk/parser work is deliberately outside those guards.
No executable, recorded media or native-device runtime is uploaded/claimed.

## Independent preceding Windows execution and protected MAIN

Preceding198 MSVC job114476838419/run38141745128 passed68 suites186.46s,
render binding0.08s. Its
[49,349-byte/three-member packet](../tests/results/M2/2026-10-11-native-monitor-render-context-windows/summary.json)
retains the complete original native job/API,19 reviewed blobs and tested
merge/tree/parents. Seven changed C++ files match frozen0a36c81. All four original
stacked jobs passed with byte-exact logs retained locally. That executes preceding
prepared context/races, not this persistence workflow, SDK devices or refreshed
MAIN admission.

PR133's ordinary protected squash `bcfa9c86c64b6f20097654fafc59eb673c477889`
matches whole tested tree/base/21 reviewed blobs from38141363539. The
[145,185-byte/eight-member packet](../tests/results/M2/2026-10-11-capture-context-current-main/summary.json)
retains all four complete byte-exact jobs/review/admission/merge receipts and BOMs.
No protection change or admin bypass occurred. PR134 now inherits that MAIN at
6b1be38 and requires its own fresh cohort38143651032. Original Windows manual-group
and parent-interruption failure causes remain unresolved by fresh greens.

## Next task and product gates

Implement explicit native startup capacity, warmup/prefill and clock-confidence
admission. Initial clock fits require an observation span; render movement needs
Start while output processing needs qualified clocks. Current steady occupancy
and bank/chunk geometry cannot be assumed sufficient for that initial interval
or whole native priming FIFO. Preserve raw pre-roll and original clocks; separate
any admitted native-only warmup from monitor/project frames and ordinary deadline
underflow. Do not weaken fit confidence or manufacture missing live audio.

Then independently qualify actual Linux/Windows SDK and installed short/long Stop,
cancel/device loss, reported failures, transaction recovery and join/retirement
before enabling the native factory. Raw/project alignment, playable-prefix admission,
EQ/mix, repeated punch/loop, processing quality, native project interchange,
European localization and full M2–M11/F/Q/C/N frozen scope remain open. See
[ADR131](decisions/131-durable-native-outcomes.md).
