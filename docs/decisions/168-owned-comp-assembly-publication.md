# ADR 168: owned final comp candidate publication

Status: accepted file transaction/recovery library; project/UI/adoption open.

Bind final files to an independently trusted canonical request containing selection,
recipe, new operation/output IDs, actual executable digest and resource/original
nominal policies. Publish new files without replacement, then publish completion
last as the paired commit. Keep partial/interrupted evidence and refuse implicit
resume or overwrite. Read-only recovery verifies original intent and every output
sample by recomputation, in addition to whole-file/sample hashes and outside scope.

Use a serialized off-GUI/disk owner, bounded slabs and cooperative stop/deadline
checks. Do not describe this library as a process sandbox or hard watchdog. Retain
request authority outside completion; versioned project registration and desktop
ownership are required integration work.

Hash the Linux running image through the kernel's descriptor path directly; do
not resolve it to a replaceable pathname first. Use that shared helper for both
comp span workers and final assembly publication. Preserve existing dependency
provenance requirements and the older nominal protocol's explicit limitations.

Verified file integrity does not establish sound quality or authorize a model edit.
Post-SRC state needs shared live/offline timing and versioned paired Undo before
adoption; the existing same-rate handle window must not be repurposed.
