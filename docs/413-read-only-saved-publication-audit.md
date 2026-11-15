# Read-only saved-publication audit

`SavedPublicationAudit` is a separate I/O-side reader for the saved restoration
journal. It accepts the strict saved record schema, including a cold controller's
zero epoch/revision, without changing the accepted-model restoration decoder.
The caller supplies the project, full selected asset descriptor, qualified media
extent/hash, original source/preparation/publication context and operation.
Journal fields cannot supply those expectations.

The reader passively decodes the pinned manifest and compares the selected asset,
including native evidence. Unrelated missing media remain permitted for inspection;
strict project opening still requires its own complete validation. Intent and any
partial/final receipt must match every supplied identity. Two receipt names,
wrong phases, conflicting owner fields, malformed records and mismatched media
are refused. Missing media and receipts remain explicit observations; a receipt
never establishes current media presence or historical publication.

Six bounded native observations cover manifest, intent, partial receipt, final
receipt, staged media and canonical media. Reverification checks pinned bytes,
current relative names, presence, the original root stamp and a freshly opened
named root before and after verification. Native links and replacement roots are
refused. This is a read-only snapshot under the serialized trusted-writer policy,
not an atomic namespace snapshot against hostile same-user concurrent writers.
Admitted manifest, record, encoded storage and native owners retire with the audit;
there is no source reopen, decoded audio, live staging or publication capability.

The existing native restoration audit target additionally exercises saved records,
Unicode paths, interrupted-state observations, every context-field mismatch,
conflicting receipts, corrupt canonical/staged bytes, cancellation, bounded
admission, root/name changes, read-only disk preservation and resource retirement.
Windows uses its actual sharing-denial semantics while pins remain live; native
Linux and Windows execution are required before integration. Existing test
deadlines, CTest inventory and strict validators remain unchanged.

This is the foundation for fresh controller reconciliation. It does not release
the existing ambiguity latch, rewrite historical publication observations, retry
a spent operation, reconstruct an exited owner, adopt a Session, implement recovery
UI or provide restart recovery. A following controller command must use the
original retained private owner, current cold context, fresh request identity and
the actual completion mutex/cancel/close ordering before releasing a guard.
