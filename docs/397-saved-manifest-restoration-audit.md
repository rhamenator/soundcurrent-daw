# Passive restoration inspection before strict project opening

`inspectSavedAssetRestoration` supplies the saved-manifest selection primitive
needed by a future controller cold-open workflow. Its caller explicitly selects
a root, expected project ID, asset ID and operation ID, plus trusted media and
project budgets and a resource ledger. No accepted controller Session or
caller-supplied asset descriptor is required. The native audit pins project.json,
admits its encoded bytes/parser/state against the same ledger, strictly decodes
it and requires the expected project identity. It resolves the selected asset
from that admitted manifest before opening any operation record or asset path.
A missing or malformed manifest cannot be replaced by intent/receipt metadata.

The existing accepted-asset audit and this passive selector share the native
inspection implementation. The accepted-asset entry point continues to require
its explicit expected descriptor to match the saved asset. Both require intent
and consistent optional receipt bindings, bound canonical/staged extent and full
SHA256, admitted path sizes, no native link traversal and final pinned-byte and
relative-name re-verification. Historical context remains evidence only.
Unrelated missing media do not prevent these observations; strict project load
continues to refuse the same incomplete project.

The saved descriptor is untrusted passive evidence. Matching its bytes and
records does not establish independent audio decoding/processing quality, an
accepted model, a restoration source capability, publication attribution or a
current owner. The returned audit owns native pins and passive state and must
stay on the caller's I/O owner; it must not be passed to GUI/audio snapshots.
There are no writes, source reopening, token acquisition, guard release, retry,
cleanup, implicit previous-generation fallback or project adoption. The existing
trusted destination-writer/snapshot limits remain; no atomic namespace promise
against hostile same-user writers is added.

The native audit corpus runs its prepared, pre-publication, post-publication and
final-state fixtures through both selector policies. Both retain independent
WAVE bytes, Unicode paths, no-write tree comparisons, exact resource/native owner
retirement, missing/mismatching media, malformed/conflicting records, strict-open
refusal, cancellation, link refusal and final verification mutation checks.
Additional saved-selector cases cover wrong project/asset/operation selection,
encoded/parser/ledger/media limits, an absent manifest with a valid journal, and
a decoded saved asset that disagrees with its original intent. Windows pins must
block native mutation; Linux changes must be detected. Actual native Linux and
Windows executions remain required before integrating this slice.

The local host has an active VM, so this slice uses lightweight source work and
hosted qualification instead of overlapping a heavy local build. Qualification
status and exact-head evidence belong in the PR and durable checkpoint. This
primitive does not yet expose a controller cold-open command or localized
recovery dialog. Fresh controller ownership, passive snapshot retirement,
pre-open model adoption and fresh-source retry remain separate acceptance gates.
The frozen Windows preview producer and delivered package remain unchanged.
