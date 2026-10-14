# ADR 164: project-owned comp selection references

Status: accepted portable reference state; process supervision/adoption open.

Store operation UUID and selection digest in the versioned main project, distinct
from immutable comp sidecars. Publish sidecars before adding/saving the reference.
Reopen only against the independently saved project digest with a fresh runtime
context and the existing recipe/source checks. Missing sidecars are recoverable
job conditions; preserve main-project readability and original audio.

Use schema 1.21 only when needed, strict fields, global identity uniqueness and
existing resource budgets. Keep registration/removal idempotence/conflict handling
and Undo/Redo atomic. Removing a reference does not cancel or delete a job. Do not
serialize runtime process handles, roots, epochs or readiness as trusted state.

Saved references establish integrity binding under the project trust boundary,
not authorship, DSP quality or final adoption. Process supervision and final
permission/curve/alignment/quality checks remain separate requirements.
