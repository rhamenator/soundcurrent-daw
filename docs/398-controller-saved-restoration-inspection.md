# Controller inspection of saved restoration evidence

`InspectSavedAssetRestoration` permits an empty controller to inspect an explicitly
selected saved project before strict Open succeeds. The caller supplies an
absolute selected root and project/asset/operation IDs with the controller's
current epoch/revision and a fresh nonzero request number. The command requires
no accepted model, session or root, no active I/O/edit gesture/inhibition, and an
available inspection job identity. It refuses replacement of an already accepted
project. Root paths are limited to4096 native code units and their captured
native storage receives a conservative shared-ledger policy charge before it
moves into the immutable request owner. This is not a whole-process RSS bound.

The selected path lives in that admitted request owner and retires before its
metadata credit. It never becomes the controller's accepted root. Inspection
shares the live inspection request/job namespace and serialized I/O slot.
Competing model/history/policy/I/O commands refuse while it runs; barriers and
exact-current inspection-job cancellation remain available. Publication-job
cancellation cannot revoke this different owner. Early refusals preserve the
prior completion and do not consume a usable request number.

On the I/O worker, the qualified saved-manifest audit pins, admits and strictly
decodes project.json, checks the selected identities and resolves its asset
from the saved manifest. Unrelated missing media do not block passive evidence.
Missing/malformed manifests cannot be replaced by operation journals. Intent,
optional consistent receipts and canonical/staged extent/full SHA256 are checked
under the same trusted project/media budgets and resource ledger. The trusted
verification hook precedes final native re-verification; stop/closing propagate
before I/O, through audit work and again at control completion.

Only a fixed-size observation DTO leaves I/O. Native pins and the passive Session
retire there before the completion reaches control or GUI. The controller checks
its actual accepted root/session/epoch/revision and dispatched job under the
existing cancel/closing mutex. The result's savedManifest flag records passive
provenance; it is set from the private job purpose, never supplied by a journal.
Error/cancel/close suppress qualified media/receipt/extent/hash observations.
No native object or passive Session is retained by the completion.

The accepted model/root/history/revisions, recording token namespace, live
ambiguity guard and historical restoration completion remain unchanged. This
command cannot release that guard, authorize a restoration source/publication,
adopt a passive model, repair or remove records, or grant checked-audio authority.
Strict Open still refuses missing media after successful inspection. A separate
explicit Open may succeed only through its normal validation and ownership
rules. The existing trusted-writer/snapshot limits remain; no atomic namespace
or future-media-availability guarantee is added.

Controller tests cover the four physical restoration states with an unrelated
missing asset, strict-open refusal, consumed/fresh requests, wrong/absent context,
relative/oversized/absent roots, foreign IDs, malformed manifests, metadata/parser
budget refusal, credit retirement, all competing commands, barriers, exact/wrong
cancellation, closing before I/O and during final verification, and native pin
mutation/retirement. Existing live inspection/reconciliation tests remain required.
Linux and native Windows must actually execute the expanded corpus before merge;
compilation alone is not runtime evidence. The recovery dialog, passive-model
adoption, fresh-source retry and full P084 remain separate acceptance gates.
The frozen Windows producer and delivered preview are unchanged.
