# ADR 159: source-bound portable comp processing recipes

Status: accepted control-side codec/sidecar; owned jobs and project integration open.

Persist the original selection and derived permissions in a versioned, immutable,
portable recipe. Reuse the existing project codec for the required source closure
and existing publication/approved-media infrastructure. Keep runtime roots/epochs
outside saved bytes and rebind them through the trusted owner on reload.

Recompute domains, exact maps, extents and permissions from the source snapshot.
Require the owner-supplied canonical digest in addition to operation/executor
identity. A valid changed transition is still a substituted selection and must
not pass using a digest obtained from its own untrusted sidecar. A build-hash
field binds an expected producer identity; binary verification remains separate.

Bound parser staging before DOM construction, retain resource credit with owners,
and preserve source/model state on refusal. The recipe is metadata, not media
admission or a project schema migration. Next bind the digest to durable owned
jobs/recovery and independently verify media/PCM before paired adoption. Post-SRC
processing needs shared execution/state representation; preserve that required
path and retain the quality gaps recorded in ADRs 157/158.
