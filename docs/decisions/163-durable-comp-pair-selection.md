# ADR 163: durable paired comp selections

Status: accepted control/recovery API; project/controller integration open.

Persist one canonical selection binding a recipe, parent and actual worker
identities, resource policy and two rederived child requests. Publish dependencies
before the manifest, without overwrite, under cooperative writer ownership.
Require an independently trusted manifest digest on load, then rederive all
requests using the current session and a fresh context.

Keep child completion verification separate from pair readiness and final
adoption. Active children cannot supply verified candidates. Completed markers
win over late cancellation; stale selections cannot be ready even without render
children. Preserve interrupted data for explicit recovery. Never automatically
resume retired jobs or infer trust from their own sidecars.

A controller must still own process lifetime, memory reservations, root/epoch and
close/cancel barriers. Add versioned trusted references to project state before
claiming durable application recovery. Integrity is separate from sound quality,
curve/alignment/scope and physical/post-SRC adoption.
