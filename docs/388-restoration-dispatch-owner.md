# Restoration dispatch owner binding

A checked replacement report establishes content identity. Restoration also needs
an accepted project, destination root, active request and exclusive autosave
inhibition. `RestorationDispatchTicket` captures those identities before an I/O
worker stages anything. It admits only the selected asset from the supplied
current model and verifies the existing replacement ticket/report against the
observed checking-process PID and resource ledger.

The immutable ticket retains bounded asset metadata, project ID, exact native root
name, epoch, model revision, request, checking job, restoration dispatch job,
autosave token, operation UUID, checked byte count and trusted byte ceiling. A
zero revision remains valid; epochs, requests, jobs and autosave tokens must be
nonzero. The model scan has a fixed 100,000-asset bound, polls cancellation and
refuses absent or duplicate asset IDs. Source-project provenance is not copied.
No source or destination path is opened by ticket admission or verification.

`RestorationOwnerView` is a borrowed view of the CURRENT accepted control owner.
The caller supplies real current closing/I/O state and acknowledged inhibition
ownership, rather than fields echoed by a child. Verification checks every
captured owner field, root, project ID, asset path/content/layout and ledger.
A copied pathname is not a pinned directory capability, and the metadata API
cannot establish that a caller really acquired a ProjectController token.

`DispatchedAssetRestoration` keeps its private ticket and physical prepared owner
together. Preparation rechecks current ownership using the approved source's
ledger and checked byte extent before invoking full finite-WAVE/hash/staging
qualification. Publication is one attempt, rechecks current ownership before
canonical directory creation, then invokes the existing exclusive publication
and durable receipt path. Stale preparation creates no operation directory;
stale publication leaves inspectable staging without a canonical asset.
Post-publication cancellation and failures preserve the physical outcome.

These calls belong on an isolated I/O worker. The accepted control owner must
serialize model/root/I/O/inhibition ownership throughout each operation; this
API does not supply the controller lock or process transport. It never changes
Session or autosave state, trusts no persisted token as restart authority, and
does not turn a check report into permission to reopen source filenames.

Tests cover project/root/asset/context changes, wrong PID/scope, absent and
duplicate assets, invalid/closing/busy owners, admission refusal and cancellation,
move retirement, stale preparation/publication, exclusive one-attempt use,
complete canonical bytes and post-publication cancellation truth. Native Windows
CI explicitly builds and selects the corpus; local cross syntax alone is not
native qualification.

P084 remains open. A controller must capture the actual accepted owner and token,
serialize publication commands, transport preparation/publication through an
isolated process, and reconcile durable evidence after child failure or restart.
Admitted passive-manifest recovery before strict Open and a complete localized
desktop workflow remain required. The strict ProjectStore gates and frozen
Windows preview are unchanged.
