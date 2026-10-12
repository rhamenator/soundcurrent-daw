# Saved native takes across application restarts

2026-10-12. The complete suite goal remains active and incomplete.

The native recording tab now provides **Open saved native take…**. Open its saved
project, choose an `original-<asset UUID>` directory in that project's `media`
folder, and inspect the verified original. The tab is available on Linux and
Windows even without a native stream adapter. Recording still requires an adapter;
inspection and export never prepare or activate a device.

A fresh controller loads the saved project and checks its expected project ID,
plain owned media directory, job name/asset ID and native declaration. It verifies
the raw common prefix and original native journal under the existing inactive-job
lease. Active writers, foreign projects, mismatched asset directories and raw-only
jobs are refused. The project and take files are unchanged by inspection.

The UI shows the stored outcome separately from current recording activity. An
inactive pending/interrupted original can be inspected; finalized, wholly finite
originals can use the existing exact-extent WAV export. No live result, device
activation, completed monitoring, project-time anchor or timeline clip is invented.
Reopening guards project/transport changes immediately after command admission,
and Keep/reset releases the selected result without deleting files. Close requests
cancel I/O cooperatively and waits for worker shutdown. Project loading itself is
bounded by the existing project policy and has no hard I/O deadline.

The immutable reopened prefix and its aliased declaration hold a resource lease
until their last consumer retires. Preparation/refresh cannot replace an unresolved
original. Command paths are bounded and admitted before their owned queue copies.
This covers the new retained-prefix owner; it does not qualify permanent ownership
of every existing snapshot, result, framework allocation or opaque backend hook.

## Tests and platform evidence

Current implementation `95bc07330cfabe7d3aa1803fcd0f3b9f3a429375` has 786
exact source/checkout inputs bound in the [qualification packet](../tests/results/M2/2026-10-12-native-original-reopen/summary.json).
Four Release suites pass 30.77 seconds: durable originals0.58, localization0.65,
recording panel9.99 and composed native controller/startup19.54. There are 85
panel checks and 1,662 durable outcome checks. Four ASan/UBSan suites pass60.76
seconds (20.33/1.93/11.49/27.01). Actual Linux application entry points link.
The original changed/dependent closure has20 fresh objects and six private
archives per profile; a final callback/return indentation cleanup recompiles the
current core object privately and reruns all four suites against the unchanged,
hash-verified objects/archives. No earlier qualified archive is modified.

Existing desktop loop-controls/UI passes2/6.40 seconds on the preceding source,
which differs only by that statement formatting. Six current Windows core,
controller, SDK adapter and fixture objects cross-compile with strict warnings and
no warnings. Native Windows runtime/link, installed and physical workflows remain
open for this source. A first strict compile reported misleading indentation in
the new cancellation boundary; the two statements are now separate. Its original
source/warning is retained. GUI TSan remains unqualified.

Linguist update, structural audit and13 corruption/refusal workflows pass:
34 catalogs,977 keys,3,168 drafts and no promoted native reviews or full language
qualification. The public packet retains commands, source/object/archive/executable
hashes, original failures and the preceding CI source bindings/logs. It contains
no executable or audio payload.

Acceptance starts with an actual simulated-SDK recording and closed controller,
then creates a fresh StudioWindow/controller with no stream factory. It reopens
the saved project/take, checks the stored outcome and sample hash, exports identical
WAV bytes, refuses conflicting project replacement and resets/closes. A separate
controller acceptance holds the prefix snapshot across reset and verifies its
credit remains until the last consumer releases it. Core cases refuse an active
writer, another project ID, a foreign path, a moved asset folder, a native take
copied into a different valid project, tiny quota and cancellation; original files
and project state remain unchanged.

The first core test attempted to overwrite an existing project with another ID;
the store correctly refused that fixture operation. The corrected case copies the
native take into another saved project's media folder and tests ownership refusal.
The first restart UI test found that the application omitted the native tab when
no stream factory existed. The tab is now created for saved-take operations on both
platforms. Original source/logs are retained. Qualification wrapper errors from an
omitted current archive and missing target list are also retained separately; the
current CMake-generated archive/link commands and objects are preserved.

## Preceding Windows export CI

PR156/run38208986550 passes the core cross-build, Windows desktop20/51.17 seconds
and Linux144/431.16 seconds, but its native job fails during compilation of the new
export fixture. The fixture's `small` variable collides with an SDK macro; the
original compiler errors are retained. Renaming it to `byteLimited` compiles under
a forced `small=char` macro that reproduces the original failure. Production export
code and the actual 73-check Windows panel workflow passed the desktop job. Native
core tests were not built/executed in the failed native job; its lack of artifacts
is recorded, not treated as passing evidence.

## Next implementation

Qualify this current source with native Windows CI and retain original runtime
observations. Add explicit native take admission at a user-selected timeline position, preserving
original capture and placement separately, then qualify native transport/alignment
and the corresponding Linux stream adapter. File selection currently exposes the
owned job directories; a richer named take browser is still required. Pending
capture recovery/copy needs its own native workflow evidence. Custom routing,
automatic idle device changes, permanent ownership, GUI TSan, installed/physical
workflows, all later milestones and full European language delivery remain open.
