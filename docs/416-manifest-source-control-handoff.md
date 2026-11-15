# Manifest-only source control handoff

A manifest-only prepared source needs its own volatile worker handoff. It cannot
use a historical restoration operation or turn a completed observation into a
live stage. `SavedManifestSourceTicket::copyControlProtocol` verifies the original
private ticket against a current native audit and actual checker result before
and after encoding. Its consumer supplies a fresh native audit, independently
observed checker PID and current source context.

The flat ASCII JSON schema `sc-saved-manifest-source-control-v1` has exactly16
fields: schema, project, asset, projectEpoch, modelRevision, request, job,
fingerprintHex, selectedLeafHex, sourceSha256, checkedSourceBytes, frames, rateHz,
channels, layoutKind and workerPid. Encoded frames are bounded to8192 bytes;
parser/output banks are admitted before allocation. Unknown, duplicate, missing,
nested, wrongly typed and noncanonical integer/hex fields are refused. Actual
cold epoch/revision zero is valid; source request/job and observed PID remain
independently checked. The checked extent comes from the actual report and must
fit the trusted native policy, never an invented manifest extent.

The193-byte fingerprint includes all seven original native destination stamp
words in explicit little-endian order, the original manifest hash, canonical
hash/presence and canonical extent. It is compared byte-for-byte with the fresh
native audit after strict decoding. A root mutation between original preparation
and consumption therefore refuses; a newly captured fingerprint cannot silently
replace the original one. Asset identity, selected source leaf, audio identity,
actual checker PID and current context must match as well. No project/source
root paths, old operation IDs, live permissions or accepted Sessions are encoded.

This is same-session metadata for a trusted parent/child channel. It is not a
persistent recovery record, cross-machine capability, source reopen permission,
controller grant or stage/publication permit. The factory returns a metadata
ticket only. A future live worker still needs a fresh approved source handle,
finite-media verification, original private parent owner, actual launch/reap and
current permit/cancel/close checks before staging or publication. Historical
control schemas and validators are unchanged.

Actual checker-backed tests cover an admitted round trip and retired credits;
all context/identity/audio/PID fields; short or altered native fingerprints;
changed roots against a newly approved audit; selected-name/hex/schema/type,
unknown/missing/duplicate/nested/non-ASCII/oversized fields; signed zero;
cancellation, foreign scope and parser/output admission refusal. The original
source/project bytes remain unchanged. Existing controller behavior, test
inventory and deadlines are preserved. Native Linux/Windows execution, exact-head
reviews and retained original CI evidence remain required before integration.
