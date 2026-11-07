# Saved source preparation without a recovery journal

`PrepareSavedManifestSource` qualifies an explicit source against a selected asset
in a saved project without requiring an existing restoration operation or journal.
Its request contains project/asset IDs, the actual cold controller epoch/revision
and a monotonic request ID. There is no operation field and no accepted Session
or synthetic nonzero epoch. Linux and Windows use the same C++ implementation.

`SavedManifestAudit` pins a bounded, strictly decoded passive manifest and the
selected canonical file if present. Unrelated missing media do not force project
opening. Existing canonical bytes must match the saved hash; missing canonical
media is an observation. A fresh named root, original root stamp, pinned and
current named manifest bytes, and canonical presence/hash are checked again.
Root paths have at most4096 native units/64 components; media paths keep their
existing portable limits. Reverification assumes serialized trusted writers,
not an atomic namespace snapshot against hostile concurrent writers. Pinned and
named bytes are hashed independently at admission and before/after source reads;
large present canonical files therefore incur repeated bounded I/O. Cancellation
is polled per chunk and this work stays on I/O workers. Windows read pins deny
concurrent writers until retirement. Concurrent project-folder mutations refuse
preparation rather than weakening the original fingerprint.

`SavedManifestSourceTicket` captures the original manifest/root/canonical facts,
selected asset hash, frame count, rate and full layout, selected source leaf and
actual source request/checker context BEFORE checker dispatch. The separate
accepted-model replacement ticket retains its nonzero-epoch rule. The real
private checker validates the selected WAVE source and reports its actual PID,
encoded extent, finite decoded audio and hash. No extent is invented from the
manifest: the checked extent is independently compared to a freshly approved
source handle, with full source hashing and repeated original-audit checks.
Wrong scope, asset, context, PID, report reference, format/content, current names
or bytes, cancellation and changed roots refuse preparation.

The controller requires an empty model/session/root, idle I/O, no gesture or
autosave inhibition and explicit absolute project/source selections. Distinct
manifest-only requests, completions and opaque readiness avoid creating a fake
historical operation. A private owner retains admitted ticket/checker/protocol
metadata; passive Sessions and native handles retire on the I/O worker. Final
completion checks current owner/epoch/revision/job and cancel/close under the
actual permission mutex. Rejected admission preserves the previous preparation;
a newly admitted I/O operation consumes it. Exact active-job cancellation and
asynchronous shutdown retire it safely. Observations are not access capabilities.

Native audit cases and actual controller/checker cases cover no journal, missing
and matching canonical media, unrelated missing assets, source/manifest changes,
wrong project/format, quotas, unavailable checker, forged PID/reference/frame
reports, current-context refusal, cancellation/close, resource and pin retirement,
read-only disk preservation and refusal to use this opaque ID with the historical
publication command. Existing test deadlines and validators are unchanged.

This change prepares and privately retains the source proof. A future qualified
manifest-only stage/publication handoff must consume that original private owner
and reapprove native source/destination facts through a reviewed live protocol.
It cannot use the existing historical-journal dispatch by fabricating an old
operation or accepted model. User-facing recovery selection, publication,
explicit strict Open/adoption and restart recovery remain separate work. No
installer or release promotion is implied.
