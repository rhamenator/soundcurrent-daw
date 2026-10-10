# ADR108: Versioned protected editing and ephemeral audition

2026-10-10 UTC. Status: implementation in progress; qualification scoped below.

Promote the original PR89 planner under distinct production names, preserving
frozen experiment files/source bindings. Add optional schema1.15 state, a distinct
v5 protocol and processor identity. Constant/context v4 keys stay unchanged.
Original owners and exact positions define normalized semantic boundaries;
untrusted persisted/request boundaries must match recomputation.

Use bounded streaming gap assembly in the disposable existing worker. The
production mode conservatively refuses nonunity gaps below the smallest retained
bank gap (2,048 frames), integer/raw-origin violations and unsupported context/
pitch/formant combinations. General and phase-preserving group modes remain
required separate work. Copying attacks or matching the prototype does not
resolve sustained phase, background onset, listening or general quality gates.

Audition must play a verified derivative before Apply. Construct and credit its
ephemeral model on the existing playback control worker, with no canonical or
Undo-stack mutation. Audition output choices are temporary. A newer canonical
revision invalidates it; Stop retires the endpoint before Apply. Playback/export
share existing prepared engine paths; no worker/codec/vendor processing enters
callbacks. Preserve cropped raw intervals through inverse-old/forward-new maps.
Retain the affine visible-length fade policy explicitly.

Acceptance includes actual child processes and full PCM comparisons, parent
receipt/source/tamper checks, shared live/export callback audits, marker UI,
resource refusal, history/persistence and audition isolation/invalidation.
Native MSVC, native Qt and installed qualification remain separate gates. Do not
publish a product preview from cross compilation or synthetic tests alone.

Next qualify and refresh installed workflow previews, including retained failed
jobs and cancellation/restart. Then continue the unresolved acoustic/general/
group modes and the complete frozen backlog. No F/Q/C/N family promotion or goal
completion follows from this integration slice.
