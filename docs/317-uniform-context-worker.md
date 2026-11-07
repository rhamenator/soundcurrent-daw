# Original-context comp rendering in an owned worker

Date: 2026-10-15. Qualified implementation: `7f1852943b08f0d95db74666b10543eb777a7691`.

## Implementation

The isolated comp worker now accepts the explicit protocol
`sc-comp-span-uniform-context-v1`, using the
`soundcurrent.comp-uniform-original-context-v1` producer from
[checkpoint 316](316-uniform-original-context-renderer.md). It processes the
original analysis context and writes only the requested candidate. This permits
short edits whose full original context satisfies the backend's minimum window.

The request binds source identity/hash/layout, raw domain, input and intrinsic
output grids, candidate phase/step/extent, rates, nominal pitch/formants, baked
speed, ancestry requirement, resampling algorithm and fixed 512-frame schedule.
The decoder rederives all 22 fields from the recipe. Changing a field while
recomputing the outer request hash still fails admission. Readiness and completion
carry the same binding. Existing source/history verification, process memory and
deadline limits, cancellation, publication and completed-output verification apply.

The older encoders, saved protocol identities and rendering behavior remain
available. The new encoder refuses protected/nonuniform geometry. No legacy job
is automatically retried using the new renderer.

## Local qualification

[Evidence index](../tests/results/M2/2026-10-15-uniform-context-worker/index.json)
records the source commit, source/executable hashes, commands, logs, owned project
and media history, requests, completion records, PCM and refusal results. Source
and executables remained unchanged throughout qualification.

- Release: the new worker plus existing uniform and clocked-protected worker
  suites pass in 66.04 seconds total; the new suite takes 33.89 seconds.
- Debug: the new worker suite passes in 83.06 seconds elapsed.
- Each configuration publishes three new candidates and rejects 33 invalid or
  canceled operations. Worker PCM matches core recomputation across 210,770
  samples. All three candidate PCM files are byte-identical across configurations.
- The short case uses real published, adopted and reopened history, seeded by
  two existing workers. Its legacy candidate requires 4,041 processing frames and
  is refused before any source read or output write. The new worker processes
  34,815 context frames and emits exactly 4,041 candidate frames, preserving the
  fractional crop and baked pitch factor 1.5.
- Cases include replay of completed operations, cancellation, waiting without a
  start command, early/late history corruption, all 22 context-field mutations,
  unknown/duplicate fields, wrong request digest and changed raw media. Completion
  verification also refuses corrupted output, changed completion geometry, stale
  model edits and cancellation. Original media/history hashes remain unchanged.
- Published file and sample hashes, frame counts, finite peaks and memory-credit
  bounds are independently audited. Two candidates preserve peaks above unity;
  the short crop is quiet. No output normalization is implied.
- Three changed C++ translation units pass MinGW syntax checks. This does not
  establish native Windows execution of the new worker.

The new suite retains a 180-second test limit, 10-second worker policy and
15-second process-observation bound. CI includes it in Linux and native Windows
selections and archives its evidence. The full configured inventory is 220 Linux
tests; the first native comp bank grows to 42, followed by the existing 93-test
bank. Five Linux shards retain every discovered test and existing job limits.
Scheduling estimates are separate from admission deadlines.

## Test-development findings retained

An initial positive test reserialized an encoded request and changed its digest.
It now sends the original encoded bytes; intentional mutations still recompute
the digest to test semantic validation. A passing larger-history probe did not
exercise the short-window gap, so the fixture now creates an actual cropped
ancestor before requesting the 4,041-frame candidate. Its optional seed setting
does not change existing fixture defaults.

The first no-start test used an artificial 500 ms deadline and expired during
history admission. It now uses the normal 10-second policy and requires readiness
before testing the missing start command. Production limits were not changed.
An audit initially required every crop to exceed unity; the observed quiet short
crop led to checking exact finite peaks per file and headroom across the corpus.
Probe failures and these corrections remain in the evidence.

## Previous native CI, retained before advancing the draft

[Original run 38722734318](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38722734318)
at `9fbb99fea827e310a1427cfadd7b9489b615f848` passed all 12 jobs. The
[CI evidence index](../tests/results/M2/2026-10-15-uniform-context-worker/original-ci-index.json)
retains all 12 complete job logs, original artifact metadata, detailed audits and
270 PNGs. All 11 original artifact ZIPs remain in the local evidence cache with
verified hashes and CRCs; the portable archive includes the reviewed corpora.

All 218 Linux tests passed across five disjoint shards. Native Windows banks
passed 40 comp and 93 other tests, with desktop banks of 22, 6 and 3 tests.
The Windows long-path observation fix and repeated speed-edit UI workflows are
now qualified on that source. The new repeated desktop cases include six
adoptions, ten completed workers, two retained mixed cases and five failure/close
cases. The largest observed stage took 6,981 ms within the 30-second bound.
Five of six exports match Linux PCM exactly; the remaining maximum absolute
difference is 1.1920928955078125e-7. These results do not qualify the new worker.

## Next delivery step and limits

Add an explicitly versioned pair-selection policy in `comp_pair_recovery` that
selects original-context rendering for uniform sides and the existing clocked
producer for protected sides. Bind and rederive the child selection on reopen,
then connect application preparation and test the actual repeated short-edit
render/review/Apply workflow, Undo/Redo, save/reopen and export. Retained mixed
sources and older saved selection policies must preserve their existing behavior.

The new worker is not yet selected by the application or new paired adoption.
A genuinely short full original domain remains unsupported. Long domains still
require processing time. Native qualification of this source, installed previews,
broader material/layout quality, localization and complete DAW parity remain open.
