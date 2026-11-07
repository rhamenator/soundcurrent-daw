# Controller-owned saved restoration source preparation

An empty ProjectController can now admit PrepareSavedRestorationSource with an
explicit absolute saved-project root, selected source file and current bounded
inspection request. The same request/job namespace, exclusive I/O slot, resource
ledger, revision/epoch checks and exact inspection cancellation used by saved
inspection apply. Early admission refusals preserve the previous private result.
Accepted new I/O invalidates that result, including a later failed Open. Closing
also invalidates live readiness. Historical completion DTOs remain observations.

The I/O worker obtains a fresh native saved audit and constructs the metadata
SavedRestorationSourceTicket before checker dispatch. It creates a private
WaveCheckController used by only that dispatch. Its first checker job is assigned
under the existing checker admission mutex; its observed job must equal the
fresh ticket context. The outer inspection job remains independently correlated
through final controller handoff. No GUI DTO or protocol PID assigns ownership,
and no accepted Session or replacement ticket is fabricated for cold recovery.

The shared checker executes the real bounded finite-WAVE decoder on both Linux
and Windows. The worker observes its actual job, PID, terminal success and zero
exit, forwards cancellation and joins it before publishing completion. Fresh
native re-verification and tuple binding precede the exact admitted protocol
copy. Closing, cancellation, changed evidence, a wrong source/report or stale
owner refuses readiness. Reaped-child facts remain visible on terminal errors;
qualified media observations and source hash do not. Native audit pins and
passive model owners retire on I/O, never on the GUI thread.

Only a private metadata owner retains the request, source ticket, checked report
and exact protocol, with ledger credit released after their storage. The GUI
receives bounded plain observations and an opaque live preparation ID. Neither
that ID nor a historical sourcePrepared completion grants permission to reopen
the source, stage bytes, publish media, adopt a model or release an ambiguity
latch. Subsequent dispatch must separately qualify fresh source/destination
capabilities and publication permission; the selected file can change after
preparation. Strict Open still requires canonical media. This preparation does
not write project evidence, complete recovery UI or complete P084.

The expanded actual controller corpus covers successful selection without an
accepted model, retained credit and invalidation, early path/context refusal,
foreign project/asset/operation, quota, absent/malformed/changed source, malformed
and identity-mismatched replies, trusted deadline refusal, competing commands,
wrong/exact cancellation, closing before spawn and after reap, final evidence
mutation/pinning, and native pin retirement. Existing shared-checker deadline
and process lifecycle tests remain required. Native Linux and Windows execution
and the broad CI gates qualify integration.

The owner's November 6 update supersedes the former Windows preview feature
freeze. Linux and Windows continue from the same C++/CMake/Qt source; new shared
changes and separately versioned qualified previews can advance together. Prior
preview deliveries and their provenance remain historical evidence. The frozen
product reference baseline, full processing/content/import/equipment/localization
scope, GPL source obligations and native release gates remain in force. Reviews
must have completed exact-head evidence: Codex plus Claude when available, with
Copilot availability determined from actual records rather than silence.
