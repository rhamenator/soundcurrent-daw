# Desktop automatic loop recording and paged take retention

P005 now has an explicit desktop recording workflow on top of checkpoint175.
This is partial functional implementation with synthetic endpoint acceptance,
not full P005/M2 or native/installed platform qualification.

## Workflow

Arm project tracks, enable and set the saved playback loop, then choose timed
recording, a positive whole number of loop passes, or looping until Stop. Loop
recording requires shared project playback and punch recording off. The selected
mode/pass count is frozen at the preparation barrier; later widget changes cannot
alter a prepared run. Every input and output route remains an explicit choice.

The recording owner freezes the original loop/input context and starts one raw
writer per input. The Linux duplex adapter validates that context before its
pool/native-filter construction and publishes it after writer startup but before
native filter activation. Publication failure preserves the original jobs and
surfaces the first error. There is no disk work in the native callback and no
per-pass worker/command inventory. Until-Stop uses checked representable clocks.

Prepared/active recording rejects structural loop edits and follows compatible
scalar EQ changes. After Stop joins native/disk owners, a complete set of original
raw receipts, end reasons and clock origins becomes a preview. Missing or failed
inputs remain diagnostic/recovery material; they cannot become a complete group.
Loop takes are never attached as audible clips automatically.

## Retention and history

The preview shows the common complete-pass count and partial final extent. Choose
a first pass, a page size and a Unicode name prefix. The page control stages at
most128 views per operation; there is no128-pass total recording limit. Arbitrary
available first-pass positions use locale-aware unsigned64-bit text. An explicit
checkbox permits retaining a partial final pass. Finish leaves original raw takes
available for recovery. Save remains explicit.

A separate typed loop-retention payload carries the immutable original context,
sources and project epoch. ProjectController checks its structural guard, charges
receipt/model/history staging and builds inactive source windows. Its disk owner
independently verifies each full raw journal/audio, matching context copy, end
reason and original timing origin before publication. It never fabricates trimmed
raw receipts or relaxes complete manual-group admission.

Successful verification adopts one semantic history edit. Allowed intervening
scalar revisions are preserved when rebuilding the candidate. Page completion is
correlated by request ID; reusing already registered raw assets on another page
does not acknowledge or discard the whole capture. Failed verification preserves
the preview and canonical model. Undo/Redo and save/reopen retain inactive views.

## Acceptance and scope

The actual Qt widgets/controllers and shared recording writers were exercised with
two delayed synthetic inputs,13 finite passes and an until-Stop partial final pass.
Acceptance includes explicit routing, invalid fractional pass count refusal,
prepared locator-edit refusal, replaced-project guard refusal, malformed context
verification failure/retry, a scalar EQ edit during paused disk verification,
5+8 retained pages, explicit partial policy, Undo/Redo, Unicode names, unchanged
raw media and Save/reopen. Tests wait for visible/enabled refreshed controls before
clicking; control-worker snapshots alone are not proof that widgets refreshed.

Ten Linux desktop/core suites passed in the development gate. Committed-source
evidence is recorded separately after source freeze. The Windows desktop CI
allowlist includes the same new synthetic GUI workflow. Native Windows duplex
recording is still missing; its normal UI disables shared recording accordingly.
Neither simulated endpoints nor compilation qualify native recording.

There are917 extracted source keys in34 partial catalogs,3,135 retained drafts and
zero native-reviewed/full-UI-qualified catalogs. New labels/errors are contextual
Qt translations, with locale integer parsing and UTF-8 take names. No complete
European-language delivery claim follows from extraction or structural validation.

Next: qualify loop recording through actual owned Linux native routes and add the
missing native Windows duplex owner. Recovered-copy reconciliation, portable job
context copying, take audition/crossfades and all broader F/Q/C/N requirements
remain open. No dependency/license change or product release upload.
