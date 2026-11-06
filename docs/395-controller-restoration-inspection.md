# Controller-owned restoration inspection

`InspectAssetRestoration` is a read-only step toward live reconciliation. A
request selects an operation and accepted asset with project ID, current epoch,
model revision and a nonzero request number. The controller checks those values
against its actual accepted model. Request numbers must increase across accepted
inspection dispatches, including project epochs. The root comes from the
controller, never the request or an old journal. The command refuses active I/O,
an edit gesture, another autosave inhibition owner, stale context or exhausted
job identity. A dedicated rejection receipt leaves the prior completion intact.

An inspection may run while the sticky restoration ambiguity latch is set. It
acquires the actual serialized I/O slot; all competing model, history, policy and
I/O commands refuse until completion. Read-only barriers and exact-current-job
cancellation remain available. A cancellation for the publication operation
cannot cancel an inspection, or vice versa. Generic recording inhibition retains
its existing behavior. Inspection neither acquires nor releases an autosave token.

The job freezes the accepted Session, root, epoch, revision, admitted request and
shared controller resource ledger. The existing I/O worker calls the native
read-only audit, then re-verifies its pinned bytes and relative-name observations.
Encoded/parser/passive-state admission uses trusted application ProjectBudget and
the same parent ledger; insufficient credit refuses, rather than decoding first
or enlarging a budget. The trusted media limit is configured in ControllerOptions.
Cooperative cancellation and closing propagate through a captured stop token;
permission/cancel/closing share the existing short mutex, with preparation and
qualification callbacks outside it. Native I/O and JSON decoding are not hard
wall-clock deadlines. These declarations do not claim whole-process RSS bounds.

Completion returns only fixed-size observation data: request/job/epoch/revision,
project/asset/operation IDs, manifest SHA256, source extent and independent
canonical/staged/receipt observations. It retains no native handles or passive
Session. The audit and its last file owners retire on the I/O worker before the
result reaches control or GUI snapshots. Completion rechecks its actual accepted
owner and suppresses observations on error or closing. Request bytes retire
before their admitted metadata credit. No GUI or audio callback performs this
work or native-owner retirement.

An observed missing canonical file remains a missing observation; a partial or
final receipt never establishes current publication. Historical tokens do not
become authorization. The accepted Session, revision, root, history, manual
manifest and original restoration completion remain unchanged. In particular,
a successful inspection does not clear the ambiguity latch or allow explicit or
timed autosave through it. A later explicit reconciliation policy must acquire
fresh ownership and independently qualify current evidence before changing that
state. This API is not a recovery dialog, source reopening capability, complete
restart workflow or full P084 completion. The frozen Windows preview is unchanged.

The controller restoration corpus adds real-worker prepared, pre-publication,
post-publication/partial-receipt and final states, using independent 52-byte WAVE
bytes and retired native staging writers. It covers stale/consumed requests,
foreign contexts, inhibition, competing commands, wrong/current cancellation,
quota, missing/corrupt evidence, final native re-verification, closing/join and
inspection of an ambiguous child completion with a dirty accepted model. Linux
mutation must be detected; native Windows mutation must remain blocked while the
audit pin is held. Existing controller/publication tests remain required, with
native Windows execution in CI before integration. Unexpected fixtures retain
the existing UTF-8 failure marker and collector policy.
