# Isolated saved staging control

The shared sc-saved-restoration-worker executable performs stage-only saved
recovery in a real native process, using SavedRestorationStage. It does not build
an accepted Session from a record, invent a positive epoch, publish media, write
receipts, adopt a project, or make a controller reconciliation decision.

The trusted serialized parent captures current source/preparation/staging request
namespaces and the actual newly started child PID independently of saved files.
It sends one bounded ASCII flat sc-saved-staging-worker-v1 request and closes
stdin. Unknown/duplicate/nested keys, invalid types/IDs/hex/native paths, extra
frames, wrong child PID and invalid current namespaces refuse. The request bank
is 64 KiB; native roots are capped at 4096 UTF-8 bytes and leaf names at 1024. The parent owns
the process deadline, cancellation, close and reap lifecycle; these calls stay
off the audio and GUI threads. The current implementation is an executable and
control-transfer foundation, not yet a new ProjectController command or UI flow.

SavedRestorationSourceTicket::copyControlProtocol exports the ORIGINAL private
root stamp, all six pinned hashes/presence observations, selected reference,
source/checker context and content/audio tuple after strict revalidation. The
sc-saved-source-control-v1 frame is temporary control metadata, never a project,
source-access, current-owner or publication capability. The receiving child
opens its own strict native audit and verifies every original field; it cannot
replace the original fingerprint with a newer audit. The fixed 441-byte fingerprint contains seven 64-bit root words in explicit
little-endian order, six 64-byte hashes and one presence-mask byte; the complete
value is hex encoded. This is same-host native identity data, not a
portable project format. Parser/output/value storage is admitted before copying
or parsing and remains bound to the same ledger AND scope.

The child freshly approves the explicit source root/leaf, binds the original
checker report, performs a full finite decode and invokes the shared native
pre-create stamp guards. Output is bounded planned-record evidence with actual
native PID and published=false (this executable has no publication path).
Error frames conservatively report whether partial staging may exist. A missing or
truncated result/error frame, abnormal exit, deadline, cancellation or closed
output pipe after launch must be treated as staging-may-exist; neither empty
stderr nor a nonzero exit proves the project was untouched. On Linux a closed
stdout pipe can terminate the worker through SIGPIPE after files were staged.
The parent must preserve and independently inspect partial evidence after reaping
the child, never infer no mutation or permission to publish from that exit. The process
retires all native owners at exit; the returned record cannot recreate them or
authorize restart/publication. The parent must validate evidence against its
independent current context, observed PID, operation and qualified content, and
must not treat stdout or a saved DTO as current ownership. Historical facts and
unknown publication status remain separate from this worker's own writes.

The dedicated saved worker defaults to a bounded 512 MiB admission ledger, capped
at 1 GiB. This preserves the existing default saved-project policy (32 MiB encoded
manifest, 64 MiB state and 256 MiB parser allowance) plus bounded frame/native
work; it does not shrink project/parser limits to fit the accepted-only worker's
64 MiB policy. Explicit insufficient caller budgets refuse before staging.
These are admission limits, not an eager allocation of the whole bank.

Controller-private preparation consumption, current-parent lifecycle enforcement,
a live one-use publication permission, canonical verification, strict Open/adoption
and localized recovery controls remain required downstream work. No project
metadata grants any of those capabilities. Linux and Windows use the same C++
implementation and require actual exact-head native execution and retained
logs/artifacts; old deliveries and all product/reference/GPL/release gates remain.
