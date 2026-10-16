# ADR 166: permission-bound verified comp assembly

Status: accepted candidate streaming operation; publication/adoption/quality open.

Assemble from independently verified owned child outputs and current source
media. Require the trusted project selection reference and both completed children
before emitting one fragment. Reverify both children and the current context
before returning. Keep the output sink unpublished until independent downstream
review and final canonical adoption succeed.

Derive the physical or post-SRC domain from the existing processing plan. For
post-SRC, position and resample the old and candidate streams independently before
the exact project-frame blend. Preserve all outside samples and floating headroom.
Use bounded, resource-charged decoding/slabs, pre-admitted output PCM bytes,
cooperative cancellation/deadline checks and credit-owning candidate reports.

The report does not certify the original nominal producer, acoustic alignment,
processing quality or future file publication. A streamed post-SRC candidate is
not an existing same-rate handle window. Shared live/offline representation,
versioned persistence, recovery, atomic paired Undo and full quality/platform
qualification remain required before this path becomes an applicable UI result.
