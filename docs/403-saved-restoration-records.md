# Separate saved-recovery records

Saved recovery needs an explicit record type that can describe an actually empty
controller. The existing accepted-project restoration record continues to require
a nonzero project epoch. SavedRestorationRecord instead uses the separate flat
sc-saved-asset-restoration-v1 schema and permits zero source epoch/revision. The
two decoders reject each other's schemas; no accepted-model or epoch is invented.

The record contains operation/project/asset IDs, source-check epoch/revision and
request/job, preparation job, current staging request/job and writer PID, a hex
encoded owned relative media path, source extent/hash, frames, rate, layout and
planned/published phase. Request/job/PID fields must be nonzero, and writer PID is
bounded to the supported native process-ID range. Values are declarations and
historical evidence. They do not prove a live process, current controller,
finite audio decode, source access, staging custody or publication permission.
No native source/root path or handle is serialized; sourceRootsPersisted must be
false. A published phase is an encoded marker, not an action or independent proof
that canonical media exists.

The serialized I/O caller must independently capture the actual current owner,
observed process, project/asset, fresh operation and qualified source extent/hash.
The verifier compares every field against those independent values. It must not
be called with context copied from the record itself. Fresh native audit/source,
private saved-source binding, isolated child ownership and one-use publication
permission remain separate gates. Historical tokens never authorize replay.

Both retained values and parser/output work have same-ledger/scope admission.
IDs are revalidated, including retired caller values; path values are copied
within the bounded grant rather than retaining a caller's excess string capacity.
The original wire encoding is ASCII JSON with Unicode paths represented as hex.
The strict decoder rejects nested/array/unknown/duplicate/missing fields, wrong
types, negative/floating/overflowing integers, invalid IDs/path/hash/audio tuples,
unsupported schema/phase and source-authority claims. Cancellation and admission
refusal retire work credit. No full-file allocation, file access or GUI/audio work
is introduced by the codec.

The controller qualification corpus uses a fresh real WAVE checker, an actual
empty controller and observed writer PID to exercise zero-epoch records. It
covers planned/published round trips, old-schema separation and retained epoch
policy, all owner fields, project/asset/operation/extent mismatch, sibling/foreign
scopes, missing and hostile fields, Unicode paths, excess caller capacity,
cancellation, quota/credit retirement, moved records/IDs and unchanged project
and source bytes. Exact-head Linux/native Windows execution, full retained CI
and completed available bot reviews qualify integration.

This metadata format is a foundation for the future isolated saved-staging
owner/control packet, not an implemented recovery dispatcher. The owner still
needs fresh source approval/full finite decoding, current empty-controller and
child binding, typed native stage-transition checks, publication permission,
canonical verification, explicit strict Open/adoption and localized controls.
The shared Linux/Windows code policy and November 6 preview-freeze lift remain;
all frozen references, full product scope, localization, GPL/source and native
release gates remain open and preserved.
