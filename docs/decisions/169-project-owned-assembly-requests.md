# ADR 169: project-owned assembly requests

Status: accepted state and sidecar design; desktop integration and audio adoption
remain separate work.

Persist a final assembly request in `media/comp-assembly-requests/<operation>.json`
before registering its independent digest in the canonical project. Schema 1.22
records the operation, parent paired selection, request SHA-256 and two optional
output reservations. Existing project formats remain unchanged when no assembly
reference is present. Loading a project does not require sidecars to be present;
recovery of the associated job does.

Use the existing project payload, codec and history budgets. Validate reservations
in the global project identity namespace, including retained take group IDs.
Duplicate identical registration is idempotent. Conflicting registration and
removing a referenced parent are refused atomically. Explicit removal affects
model references only. Preserve sidecars, candidates and original media for Undo
and recovery. Future adoption must consume output reservations atomically with
asset insertion; an output cannot simultaneously be reserved and an active asset.

Publication uses a cooperative exclusive sidecar writer lease, file flush and
no-overwrite publication. Identical existing bytes are accepted; changed bytes
are not replaced. The canonical owner must recheck its live model and save the
reference after sidecar publication. An orphan sidecar after a failed save confers
no authority. Project load resolves the saved digest and rederives the request
against the referenced paired selection and fresh context, never using a digest
from completion to authorize itself.

This adds durable request identity, not a desktop owner, process sandbox, power-loss
guarantee, sonic-quality approval or permission to activate audio. The existing
physical-rate handle window remains unsuitable for project-rate post-SRC output.
