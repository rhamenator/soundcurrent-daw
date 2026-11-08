# Manifest-only controller staging ownership

`StageSavedManifest` consumes the exact current private manifest preparation from
an empty serialized ProjectController. Its distinct preparation/request/job
namespace cannot be supplied by historical recovery commands or reconstructed
from a passive completion. Rejected commands preserve the private preparation.
Successful I/O admission transfers it into the owned job before clearing public
readiness. Paths, source/checker objects and control frames come from that
original private owner, rather than the stage command.

The I/O worker runs the shared manifest parent process supervisor with the
original ticket/control/checker and immutable selected names. All competing
mutation and I/O commands are refused until actual process cleanup and native
owner retirement complete. Only passive barriers and correctly scoped cancel
commands can progress. Close/cancel never convert an in-flight operation into a
verified stage. Finish rechecks the actual empty owner, epoch/revision and job,
and preserves conservative process/staging facts while clearing verification
flags on error or cancellation.

The distinct completion is passive evidence, not a publication permit. Its
requestSent field means input write attempted or queued, without proving child
consumption. A normal successful child requires both normalExit=true and
childExit=0. A killed child's raw exit-code observation alone is not evidence of
normal execution. Prepared readiness is spent even when staging fails; a fresh
explicit preparation is required for another stage.

Fifteen real controller workflows cover successful checking/staging, competing
command exclusion, wrong cancel and preparation IDs, foreign historical fields,
pre-input/pre-EOF/late cancellation, close, malformed terminal PID, deadline,
child admission and failed launch, changed source, missing private preparation,
spent-ID replay, escaping standard/nonstandard epilogue exceptions, native owner
retirement and memory-credit return. Caller-owned completion storage preserves
actual process facts even if an exception escapes the supervisor epilogue. The existing
controller CTest deadlines and CI shard inventory remain unchanged. Actual Linux
and native Windows execution and full original audit retention are qualification
gates; source preparation alone does not establish execution.

No GUI recovery workflow, canonical publication, accepted project/root/history,
autosave ownership, installed helper qualification or release is granted by
this controller API. The new manifest helper must satisfy native installed
package/source/provenance gates before an installed GUI route depends on it.
