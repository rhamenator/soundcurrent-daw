# PipeWire take admission and portable project evidence

Checkpoint 235 adds explicit timeline admission to the typed recording controller
and main window. The full frozen-reference goal remains active.

## Workflow and project state

The worker verifies the project-owned original job, expected declaration, finite
finalized raw extent, Float32 WAV geometry and hashes. It pins and rechecks all
files and reopens named paths before returning a verified structural edit. The GUI
submits that edit through the existing project controller; admission is explicit
and undoable. Placement is the user-entered project frame, without deriving
alignment from native timestamps or queued monitor frames.

A complete take preserves eight references: owner/run/journal manifests, raw packet
and capture/render cycle records, and the UUID intent/outcome artifacts. A valid
state without a terminal outcome can preserve seven. Each reference carries size
and SHA-256. Project schema 1.19 supports these exact ordered lists; existing
Native-only projects still emit 1.18 and retain their strict legacy rules.
Save/reopen media validation verifies the referenced files. Inactive saved-take
selection distinguishes plain PipeWire versus Native state and refuses ambiguity;
it does not activate a driver or fabricate another backend's clock facts.

Admission and original export default to a 64 MiB resource ledger to accommodate
the existing bounded state-inspection workspace. Approved-file counts and byte
limits remain bounded. Pinned/hash verification follows the existing cooperative
project ownership contract; it is not an atomic snapshot against hostile writers.

## Acceptance evidence

Source f3bc0aa binds 837 selected source/checkout inputs. The six targeted Release
gates pass in 38.70 seconds: typed controller, main-window recording panel, session
state, unchanged Native startup/controller, and both localization audits. The Qt
suite passes in 13.27 seconds and exercises PipeWire Prepare/Record/Inspect/Export/
Add/Save/Undo/Redo/Reopen/Close. A subsequent portability case passes in 2.22 seconds:
a copied Unicode project root retains state, admits the same hashed take, exports
identical raw bytes, and refuses an original job outside the copied root.

Processed export uses the shared engine, explicit placement, a 113-frame block,
Float32 headroom, and measured 1 kHz attenuation. Invalid evidence count/order,
UUIDs, size bounds, duplicate paths, legacy schema smuggling, foreign project,
quota, cancellation and corrupted outcome cases are refused. Existing main-window
Native cases remain in the same suite. These are synthetic acceptance tests;
current native Windows runtime, installed packages, hardware, alignment, hotplug,
processing-quality coverage and reviewed European translations remain open.

Initial build failures (stale generated include path and incorrect test-history
API calls) are retained alongside their corrected builds. No queue, timing,
validation or audio-quality thresholds were relaxed.

The genuine private PipeWire 1.6.2 controller fixture passes: 99,072 bit-checked raw
frames, 8,192 processed/sink frames, 387 capture and 408 render receipts; explicit
placement 257, eight evidence files, canonical save/reopen and callback audit zero.
Its owner-only dummy-server route touches no physical endpoints or default settings.
Seven current admission/state/controller/test Windows objects compile without
warnings. This is compile evidence; native Windows runtime remains pending.

## Original PR168 CI and Windows path correction

Original run 38327109970 at c291a51 is retained, not replaced by a retry. Both Windows
jobs failed during checkout because public evidence filenames were too long.
Ten compressed files are now aliased to short names with unchanged compressed
bytes and hashes; the previous packet index includes the original-name mapping.
The cross-build passed. Linux passed 153/154 in 446.93 seconds; its controller test
failed during finalization. The uploaded Linux artifact has only CTest logs, not
failed take files, so that cause remains unproven. New test diagnostics expose
specific finalization facts for future failures; local passing runs do not resolve
the original failure. Earlier clock/pool/owner failures also remain unresolved.

## Next implementation task

Qualify this admission/persistence path under current sanitizers and independent
native Windows CI; retain original failures before corrections. Then complete
native playback of admitted takes and remaining M2 recovery/alignment/hotplug
workflows. M2–M11, functional/quality/content/native compatibility, X004/X005 and
Linux/Windows/all-Europe requirements are not complete.
