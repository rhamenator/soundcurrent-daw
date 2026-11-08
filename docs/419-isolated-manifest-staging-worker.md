# Isolated manifest staging worker

The production `sc-saved-manifest-worker` uses the shared bounded worker framing
and native `SavedManifestStage`, with a distinct manifest-only protocol. It is
built independently of tests. Its private compile mode cannot be combined with
publication or qualification modes. Historical v1 staging and v2 publication
workers retain their existing schemas and behavior.

The caller owns the process deadline, output bounds, cancellation and reap on
its serialized I/O worker. It must retain its original native audit and freshly
approved source pins through launch and reap. No controller command or public
snapshot is added here; integrating the actual private controller preparation
and supervisor is the next step. The new executable is not yet a packaged UI
dependency or a release/installer qualification claim.

Input is one ASCII newline frame, at most65536 bytes, followed by EOF before
filesystem access. Manifest-mode Windows stdin is binary so Ctrl+Z cannot
substitute for physical EOF and hide trailing input. The exact19-field schema is
`sc-saved-manifest-staging-worker-v1`: protocol, project, asset, operation,
rootHex, sourceRootHex, sourceLeafHex, sourceControlHex, checkReportHex, checkPid,
maximumBytes, projectEpoch, modelRevision, sourceRequest, sourceJob,
preparationJob, request, job and workerPid. Paths/protocols use bounded lowercase
hex. Roots are explicit absolute choices; the leaf is a plain filename. The
caller supplies the original source control frame and actual checker protocol,
independently observed checker PID, current context and observed new child PID.
Input metadata alone cannot supply controller authority.

The worker admits its frame/parser banks before reading, enforces trusted CLI
memory/source limits, strictly refuses unknown/duplicate/missing/nested fields
and wrongly typed integers, and checks the requested PID against its native PID.
It decodes the actual checker report, inspects a fresh native manifest audit and
matches the original source fingerprint before opening the explicit source.
The native stage rechecks the source with a fresh finite decode and creates only
an exclusive planned operation. It reverifies its live original owners before
emitting a bounded `sc-saved-manifest-staging-result-v1` containing its actual
workerPid, published=false and recordHex. Errors use the distinct
`sc-saved-manifest-staging-error-v1` with conservative stagingMayExist and
published=false. On success the child's native owners remain through result
output; on exceptions they unwind before the error frame. The parent must keep
its own original pins through reap on both paths. No permission or publication
frame is accepted.

The result is read-only stage evidence after the child exits, not a live stage
capability, publication permission, adopted Session or restart/replay token.
Cancellation can leave partial evidence; the future supervisor must preserve
positive PID/exit/staging facts, verify current owner state and never reconstruct
an exited owner or silently clean unrelated files.

Actual-process tests hold original parent pins through child reap and check a
valid staged record/bytes plus old protocols, wrong worker/checker PID, duplicate
or historical fields, changed original root, stale context, altered checker
facts, byte/memory limits, cancellation before EOF, trailing/oversized/non-ASCII
input, an existing operation, path traversal, old inner control schema and
Ctrl+Z followed by trailing bytes.
Original source/project facts and parent credits are preserved. Existing CTest
inventory/deadlines and validators remain unchanged. Exact-head Linux/native
Windows execution, Codex/Claude reviews and original evidence retention remain
required before integration.
