# Isolated restoration worker endpoint

`sc-asset-restoration-worker` prepares exact replacement media in a child
process. Preparation emits READY; only a matching publication permit followed
by end-of-input can reach canonical publication. This development endpoint is
not installed or connected to a recovery dialog. The accepted controller
transaction and complete P084 workflow remain open.

The trusted temporary stdin channel accepts one ASCII JSON line, at most 65536
bytes excluding LF. Its exact twelve fields are `protocol` (the string
`sc-restoration-worker-v1`), `project`, `rootHex`, `sourceRootHex`,
`sourceLeafHex`, `recordHex`, `checkReportHex`, `checkPid`, `checkJob`,
`dispatchJob`, `autosaveToken`, and `maximumBytes`. Unknown, duplicate, missing,
nested, non-ASCII and wrongly typed values refuse. Integers must be unsigned
JSON integers. Native Unicode paths use lowercase UTF-8 hex; roots must be
absolute and the selected source leaf a single basename. The Planned record
uses the existing strict restoration codec and carries asset/content and
versioned owner context.

`checkReportHex` transports the prior strict WAVE checking report. Its captured
`checkPid` belongs to that original checker, not this restoration child. The
parent must independently capture and bind that checking process/report and
its accepted dispatch ticket; matching echoed fields are not independent
proof. The restoration child opens the explicitly selected approved native
source and freshly decodes the complete finite WAVE, verifies saved metadata,
whole-file extent and SHA, and stages/readbacks exact bytes through the existing
physical restoration core before READY. It does not edit the manifest.

READY is a flushed line of at most 32768 bytes with exactly `protocol`
(`sc-restoration-ready-v1`), `workerPid` (actual native self PID), `operation`,
and `recordHex` (actual prepared Planned record). The parent must validate all
fields against its own process and accepted immutable expectations. Preparation
metadata alone does not prove canonical publication.

The second stdin line is the existing `sc-restoration-permit-v1` message, bounded
by 16384 bytes. The parent closes stdin after sending it. The child requires
EOF before publication, rejecting trailing frames. The permit binds actual
self PID, exact operation/content/root, epoch/revision/request, checking and
dispatch jobs and autosave token. Missing, stale, malformed or foreign permission
cannot reach canonical rename. An unresponsive parent can leave a prepared
child waiting: framing does not supply a deadline or cancellation watchdog.

After publication, stdout emits exactly `protocol` (`sc-restoration-result-v1`),
`workerPid`, `operation`, `published`, `receiptPublished`,
`postCommitFlushFailed`, `durability`, and `recordHex` (prospective Published
record). These separate visibility, final receipt and durability outcomes;
Linux directory flushing and Windows file flushing retain their existing
meaning. The parent must bound/decode the terminal frame and reap the child
before opening files whose Windows handles it holds. Exit status or an absent
reply cannot prove that publication did not occur.

Refusal uses bounded stderr JSON with protocol `sc-restoration-worker-error-v1`,
stable `messageId` `restoration.worker_failed`, numeric `errorCode`, actual
`workerPid`, and conservative `reconciliationRequired`. No native path or
exception text is emitted. That boolean is diagnostic, never a substitute for
fresh durable/canonical inspection after failure, cancellation or crash.
The stable message ID is not yet a delivered localized UI message.

CLI `--memory-bytes` admits 4–128 MiB (default 64 MiB), and
`--maximum-source-bytes` sets an independent nonzero source ceiling (default
1 GiB, at most signed 64-bit range). The request cannot increase that ceiling.
Framing/parser/output and existing core banks use the declared resource ledger;
this is not an OS sandbox or a hard allocator/RSS limit. No real-time thread
performs this work. Native destination trust, no-overwrite publication,
inspectable staging, journal evidence and reconciliation contracts from
386–389 still apply.

The future parent must acquire actual I/O/autosave inhibition and serialize
accepted model/root ownership through child completion, enforce deadlines,
close channels on cancellation, terminate/reap safely, and reconcile committed
bytes after any ambiguous result. Cancellation after permission may race with
rename. The child creates only an immutable snapshot; it cannot acquire or
validate a live ProjectController lock on the parent's behalf. Passive-manifest
recovery before strict Open and user-facing/native workflow qualification are
not supplied by this endpoint.

The subprocess corpus obtains an actual checker report and observes actual
restoration PIDs. It compares complete independently generated media bytes,
checks no publication without permission, malformed requests/permissions,
changed and nonfinite sources, existing/racing destinations, kill after READY,
and a deterministic crash after canonical rename. Only a separate qualification
binary enables that crash; production rejects its switch. The crash leaves
canonical bytes and receipt.partial without a successful terminal result,
explicitly demonstrating why parent reconciliation is necessary. Hosted native
Windows execution remains a gate; MinGW syntax checking alone cannot qualify it.
No installed package, preview producer, catalog or release claim changes.
