# Saved restoration source-report binding

`SavedRestorationSourceTicket` is an I/O-only metadata primitive for the next
cold-recovery source workflow. Before dispatching a fresh explicitly selected
checker job, its trusted owner supplies the current epoch/revision, nonzero
request/job, selected relative filename and a current native restoration audit
in the same resource ledger. Epoch/revision zero remain valid for an empty
controller. The ticket re-verifies that audit, then captures its native directory
stamp, all six exact manifest/intent/receipt/staged/canonical hashes and presence
states, expected source extent/hash/frames/rate/channels, filename and fresh
owner context. It does not copy a passive Session or retain native handles.

The selected name retires before the ticket's admitted metadata credit. The
native fingerprint is private and originates in the audit, rather than caller
DTO fields. The existing accepted-model replacement ticket keeps its nonzero
project-epoch policy; cold recovery does not fabricate an accepted model or
relax that policy. The audit library explicitly links the existing strict WAVE
report implementation; no new report schema or audio decoder is introduced.

`verifySavedRestorationSourceReport` requires the current serialized owner's
context, the actual observed PID from its completed checker job, a strictly
decoded checked report and a current same-ledger audit. It rejects stale owner
fields, wrong PID/filename/scope, moved owners, a different native directory even
with identical saved bytes, any changed audit fingerprint or visibility, and
report extent/content/audio-identity mismatch. Native re-verification is fresh;
old observations and historical journal tokens are not current ownership.
Cancellation and resource refusal propagate without changing evidence.

`copySavedRestorationSourceProtocol` performs that verification and copies the
original admitted checker bytes, preserving whitespace, exact PID and protocol
representation instead of reconstructing JSON. The copy has independent
same-ledger credit. The report boundary is trusted after strict decoding and the
owning checker has actually decoded finite audio; this primitive is not an
independent second decode. Its caller must establish terminal success/reaping,
assign the actual checker job atomically and retain current exclusive ownership.
The primitive itself neither assigns a job nor starts a process.

This ticket and copy grant no source reopening, destination access, staging,
publication, model adoption, token/latch release or cleanup authority. They are
not permission to replay the original restoration operation. A subsequent owner
must separately qualify the selected source capability, current destination,
publication permission and final controller state. Existing trusted-writer and
snapshot race limits remain; no atomic namespace or future-availability promise
is added. The controller cold-recovery preparation/dispatch and localized UI
remain separate work, and this does not complete P084.

The actual controller qualification corpus exercises the primitive with an
empty controller, native saved audit and a newly executed default WAVE checker
whose real PID, completed job and zero exit are observed. It preserves an exact
whitespace-bearing reply through checker retirement, then verifies/copies it.
Adversarial cases cover all owner fields, zero request/job, selected names, wrong
PID, foreign/sibling ledgers, cancellation, quota/credit retirement, six mutated
strict report fields, moved tickets/reports, re-auditing the same root, an
identical-byte clone under another native directory, and fresh audits after
manifest/intent/visibility changes. A different actual finite WAVE source must
also be refused. The tests preserve project/source bytes and the absent accepted
model; native Linux and Windows executions are required before integration.
The frozen Windows preview producer and delivered package remain unchanged.
