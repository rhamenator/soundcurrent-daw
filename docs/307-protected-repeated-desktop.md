# Repeated protected edits through the desktop

2026-10-15. Implementation source `1f74c12`. Full DAW scope remains incomplete.

The desktop acceptance bank now starts from real worker-produced protected
assemblies and drives the timeline's modal **Save and render**, then **Review** and
**Apply**, three times at each of 44.1, 48 and 96 kHz. The final generation retains
an assembled take and regenerates only its peer. Original raw/nominal history,
protected span v2 and fresh job identities are checked in the actual desktop's
requests. Generated samples within each protected region, excluding the admitted
halo, must match original raw samples bit for bit.

Each adoption checks consent, exact Undo/Redo, explicit save/reopen, unchanged
output outside the union of old and new fade envelopes, floating-point headroom,
and live/offline equality. Independent source PCM checks remain separate from
fade-envelope comparisons. Export hashes are compared with checkpoint 305's
engine path. Cancellation, stale arrangement, damaged ancestry before rendering,
damaged ancestry after Review, and closing a pending Apply preserve the active
arrangement. The actual window's project resource ledger returns to zero after
its controllers and snapshots retire. Stage observations retain the original
30-second limit; no worker or existing suite deadline was extended.

The first six-case desktop probe passed its audio/state checks but its screenshot
showed a stale “Transition applied” status on the next reviewed render. Accepting
a new render now clears that previous job's message. The regression requires
“Audio verified” before Apply on every generation. Original screenshots remain
retained. Qt Linguist extraction and catalog checks were rerun: no message keys
or translations changed, and the 34 catalogs remain partial/unreviewed. There is
no new dependency, persisted format or real-time processing change.

## Qualification

Four Release suites pass in 285.687 seconds, including existing protected and
uniform repeated desktop banks and localization. Two Debug suites pass in
361.594 seconds. Sources and binaries stayed unchanged during each qualification,
and selected targets had no pending build work. Each new bank performs nine
adoptions, three retained mixed cases and five refusal/close cases. The nine
adoptions use 15 successful, reaped workers; the actual project ledger releases
all credits at window destruction.

All nine new export hashes match Debug, Release and checkpoint 305's engine
exports. The nine legacy repeated exports remain unchanged. Each configuration
checks 32,000 protected raw samples, 1,640,832 unchanged audible samples and
1,810,560 live/export samples across successful adoptions. Fourteen stage
observations finish within the unchanged 30-second bound; maxima are 21.561
seconds in Debug and 8.893 seconds in Release. This is scoped synthetic evidence,
not general performance or perceptual quality admission.

The [evidence index](../tests/results/M2/2026-10-15-protected-repeated-desktop/index.json)
references the portable acceptance archive, full projects/worker logs, source and
binary hashes, screenshots, original status-message observation and audit scripts.

## CI and preceding native evidence

The preceding original run [38686371153](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38686371153)
at `c043877b6a39759630dffb5085d17b6f4627f000` passed all seven jobs:
200 Linux CTest cases plus every archive check, 25 native comp suites, 93 other
native core suites, 28 native desktop suites, and the Windows cross-build. The
previously failed one-frame startup test passes. All seven original artifact ZIPs,
terminal APIs, full job logs and 146 PNGs were retained and audited before advancing
the PR. Fifteen deployment payloads and 1,739 source files match the original
native build and Git checkout filters. This is separate from installed audio tests.

The native history bank completes nine adoptions, three retained mixed cases,
15 successful workers and two required worker failures, with cleanup and protected
raw checks passing. Six 48/96 kHz export files match Linux exactly. The three
44.1 kHz exports differ in 354, 355 and 354 samples, respectively, by at most
`1.1920928955078125e-7`, with RMS differences below `8.23e-10`. Raw inputs are
identical across platforms. The original 44.1 kHz nominal render already differs
in 177 samples before repeated editing, with the same maximum difference. The
exact arithmetic cause is not isolated. Within each platform, protected raw,
unchanged-output and live/export assertions pass. No cross-platform bit-exactness,
perceptual equivalence or quality tolerance is inferred. The initial overbroad
cross-platform hash assertion, comparison scripts and actual PCM results are
retained; no acceptance-test assertion or DSP code was weakened.

The [original CI index](../tests/results/M2/2026-10-15-protected-repeated-desktop/original-ci-index.json)
contains the complete native protected-history corpus, original logs, screenshots,
source/deployment audit and numerical comparison. Full original ZIPs are also
retained locally. The portable CI archive is 55,415,525 bytes, SHA-256
`d412e01c236f2bcca7e9f3c3e756e96fa617b45c23c74341d22ce216c3290137`.

The preceding desktop job used approximately 20 minutes. Adding this new bank to
that same job would leave insufficient margin. Native desktop CI now has two
independent 20-minute jobs: the original target/test list and the new repeated
protected bank. Both must pass the original branch-protected **Windows desktop
import and localization tests** status, now an aggregate. Both retain independent
failure evidence; fail-fast is disabled. No existing test, upload or deadline is
removed or relaxed. Workflow parsing and exact target/pattern comparisons pass.

All 200 observed Linux test durations now inform the three-shard scheduler. The
new desktop bank has no remote timing yet, so its explicit provisional weight is
twice its local Debug duration: 721.64 seconds. The 201-test inventory is assigned
exactly once (1/101/99 tests, approximately 722/688/688 estimated seconds). The
required Linux aggregate and 30-minute job limits remain unchanged. The scheduler
also passes stable-order, disjointness and unknown-test coverage checks. Fresh CI
must qualify both the new native bank and these scheduling changes.



The test runs offscreen, without physical audio, installed app changes or a VM.
Native Windows execution of the new desktop bank remains pending its new CI run.
Fractional protected history, changed backend clocks, altered baked pitch,
cropped protected windows and independent listening/processing-quality gates
remain required. This does not complete M2, M3–M11, F/Q/C/N, X004/X005 or all-Europe
localization.
