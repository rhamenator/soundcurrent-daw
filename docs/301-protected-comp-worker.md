# Protected comp worker requests and verified output

2026-10-15 UTC. Source `b1029c4667e7164914a28b96a34c0580d152e768`.
Full frozen scope remains incomplete. This increment connects the owned protected
span core to the isolated worker; desktop selection and adoption remain pending.

## Implementation

`CompSpanRequest` owns a variant of the uniform and protected plans. Explicit
`encodeProtectedCompSpanRequest` selects protocol `sc-comp-span-protected-v1`
and processor `soundcurrent.comp-span-protected-rubberband4-r3-v1`. The legacy
encoder retains its existing refusal of protected geometry. Existing v1/v2
requests and rendering use their original algorithms.

The protected request binds original source identity/hash, raw extent and clock,
channel layout, exact raw/output map, semantic anchor identities and spans,
protection and halo policy, geometry/processor identity and the fixed render chunk.
Canonical geometry is resource charged before serialization and SHA-256 hashed.
The decoder independently derives the geometry from the authenticated recipe and
compares the typed binding. Intent, ready and complete messages rederive the same
binding alongside recipe/request digests and the executable hash. The new source
algorithm identity is `soundcurrent.raw-absolute-frames-v1`.

The worker retains its existing process limits, watchdog, source admission before
and after rendering, active-job ownership and atomic completion publication.
Rendering shares the core implementation and preserves floating headroom.
Completion verification checks the protected output with the same ownership,
source, WAV, current-project and resource guards used for uniform output.
The original integer, same-clock, zero-pitch protected subset is supported;
fractional/post-SRC timing and prior handle/history regeneration remain refused.

## Acceptance

The new bank uses real protected nominal PCM at 44.1, 48 and 96 kHz, one or two
semantic anchors, stereo attacks and a tone bed. Each configuration runs 69 owned
worker processes: six successful outputs and 63 required refusals. It compares
622,848 output samples with full in-process recomputation and independently checks
12,800 protected samples against original raw PCM. Halo-only and semantic-owner-only
changes alter the binding even when the timing map is unchanged.

Refusal cases include edited geometry/extent/clock/count/chunk fields, wrong JSON
number types, duplicate keys, protocol downgrade, wrong request/executable hashes,
duplicate job ownership, cancellation, missing start acknowledgement and source
changes both before admission and after readiness. Verification rejects active
workers, modified output/markers/raw media, canceled verification and stale project
state. Every child is observed and reaped; the test refuses observation timeout,
pending retirement or truncated output. Resource credits and unchanged source/
project state are checked. Complete original stdout/stderr/packets and PCM are
retained with qualification receipts.

The final qualification at the source commit passes two Debug suites in 44.475
seconds and five Release suites in 250.536 seconds. Source and executable hashes
remain unchanged, and the selected build targets had no outstanding work before
CTest. All six protected output hashes match between Debug and Release. Six
legacy v1 output hashes, nine v2 completed hashes, nine repeated core exports and
nine repeated desktop exports match their corresponding earlier fixture families.
The desktop bank checks 502,422 unchanged outer samples and 508,182 live/export
samples; all 14 render-stage observations remain within the unchanged 30-second
limit, with a maximum of 7,702 ms. The three modified C++ translation units also
pass MinGW syntax checks; this is compilation evidence, not native execution.

[Evidence index](../tests/results/M2/2026-10-15-protected-comp-worker/index.json)
records the full qualification corpus, PCM, process logs and source/binary hashes.
The archive contains 4,273 original files represented by 1,203 unique payloads,
20,698,761 bytes, SHA-256
`1fb3c8b520d83c0d2be16db7b4d93f89aa7930a53f403839cd5a7d66ac328148`.
Every payload hash and ZIP CRC is verified. The initial Release diagnostic is retained locally and is not substituted for the
final source-bound qualification. The original fixture indentation warnings were
corrected before the implementation commit and final tests.

## Remaining integration

`prepareCompPairSelection` deliberately still uses the legacy encoder. Next add
explicit protected selection and deterministic snapshot rederivation, then test
paired recovery across absent/active/waiting/canceled/interrupted/complete/stale
children and corrupted manifests. Require verified paired publication and review
before model adoption. Follow with desktop Review/Apply, Undo/Redo, save/reopen,
repeated history and shared playback/export acceptance.

Copied attack samples and matching implementation output do not establish
independent processing quality. Gaps, joins, real music and recording-group phase
remain subject to [ADR 104](decisions/104-local-warp-and-group-quality.md).
Native Windows execution of this new worker path, installed delivery, fractional
and changed clocks, history, arbitrary channels, all-Europe and the full
M2–M11/F/Q/C/N/X004/X005 scope remain open. No installed application, audio route,
VM or equalizer was changed. No production UI text changed in this increment.
