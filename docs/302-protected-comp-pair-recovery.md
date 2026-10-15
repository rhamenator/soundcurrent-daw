# Protected comp pair selection and recovery

2026-10-15 UTC. Source `587b5033c4aeb9dd55a34e13ba902fb710dbdfb3`.
The full DAW goal remains incomplete. This increment extends saved paired-render
recovery; protected paired assembly and the desktop adoption path remain pending.

## Implementation

`CompPairSpanPolicy::ProtectedOriginal` explicitly enables original protected
clips. Existing callers retain `UniformOnly`. Selection uses the original clip's
semantic warp declaration, including its original nominal clip when history is
present. It never infers protection from a slope or retries failed uniform work
with another algorithm. The protected adapter retains its explicit refusals for
history, fractional positions and changed clocks.

The policy has its own `sc-comp-pair-selection-protected-v1` saved format. Each
needed side chooses the protected worker when semantic protection is present;
otherwise it uses the existing uniform worker. Existing-media sides still need
no child. The manifest binds recipe, worker, policy and child request digests.
Loading derives the selection and exact child request bytes again from the original
recipe, then compares them with the independently trusted project reference.
The old format and default policy preserve their existing serialization and path.

Publication and recovery reuse the original immutable sidecars, ownership locks,
current-project/context checks, source readmission and verified completions.
Active, absent, waiting, canceled, interrupted, stale and invalid results cannot
authorize paired readiness. Late cancellation does not hide already completed,
verified output. This API supplies candidates; it does not adopt canonical audio.

## Acceptance

The protected bank uses real isolated children at 44.1, 48 and 96 kHz, with the
one/two-anchor stereo fixtures from checkpoint 301. Each configuration runs eight
workers: six successful renders forming three pairs, one canceled worker and one
watchdog retirement while waiting for start. Each pair is persisted, reopened
through the trusted project reference and relocated into a Unicode path.

The bank checks active-state inspection and fresh-model start authorization,
idempotent publication/start/cancel, readiness only after both valid completions,
missing/wrong project references, stale epochs, changed manifests and requests,
corrupted output/raw media, canceled verification and late cancellation. Switching
protected format to uniform is refused even when the test supplies a newly
computed trusted digest, because recipe-derived protected requests cannot be
reconstructed as uniform requests. The default policy also rejects the protected
fixtures. Interrupted-after-start inspection uses a clearly labeled retained-state
fixture after actual watchdog retirement; it is not a crash-during-render test.

Existing uniform-pair recovery runs separately, including existing-media sides.
The fixture uses Windows extended paths for its direct file reads/writes. Driver
exceptions kill and reap the owned child, retaining both pipes. Worker observation
checks retirement, timeout and output limits. No product GUI text changed.

Final qualification passes two Debug suites in 26.162 seconds and four Release
suites in 52.082 seconds, including uniform paired assembly and paired-history
regressions. All six protected output hashes match between configurations and
match checkpoint 301's fully compared worker output. All children retire without
observation timeout or truncated output. Source and binary hashes stay unchanged.
The modified C++ implementation and fixture also pass MinGW syntax checks; native
Windows execution of this new path remains pending.

[Local evidence index](../tests/results/M2/2026-10-15-protected-comp-pair/index.json)
identifies the original PCM, saved projects, requests/completions, driver and worker
logs, final qualification receipts, source/binary hashes and audit scripts.
The local archive contains 1,776 original files represented by 553 unique
payloads, 12,216,829 bytes, SHA-256
`11cf6abd51f527ed8d980b6b911608e16d6987d7b079a29c448110da7ccd2b12`.
Every payload hash and ZIP CRC was verified.

The initial Release probe passed before the final process-report refinement and is
retained as diagnostic evidence. Final qualification is tied to the committed
source and records no outstanding build work plus stable source/binary hashes.

## Original CI and evidence-retention correction

[Original CI 38674892049](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38674892049)
at `4ff3bea` completed successfully across all six jobs. This revision contains the
protected core from checkpoint 300, but predates the protected worker and pair.
All terminal APIs, six full job logs, six original artifact ZIPs and 94 PNGs are
retained and audited before advancing the draft PR.

- Linux: 195 unique tests, split 98/97, assigned exactly once; aggregate passed.
- Native Windows: 21 comp tests and 93 other tests passed. Desktop: 27 tests passed
  and the whole job succeeded. The cross-build passed separately.
- Native repeated desktop: nine adoptions, three mixed retained cases, 15 successful
  reaped workers and five failure/close cases. Checks cover 502,422 unchanged outer
  samples, 508,182 live/export comparisons and released resource credits. Fourteen
  stage observations pass; maximum 5,720 ms against the unchanged 30-second bound.
- Two actual native Qt screenshots were inspected: readable Review/Apply controls
  and the applied adjacent-clip timeline with Ready status. These are offscreen
  workflow tests, not installed audio or whole-window usability qualification.
- The deployment input audit checks all 15 payloads, both independently built
  media dependencies and all 1,722 corresponding source files against the tested Git tree
  using actual Windows checkout filters. It is not an installer qualification.

The native protected-core test passed in 3.85 seconds, but its detailed PCM/report
folder was omitted from the workflow's artifact upload list. No detailed native
PCM audit is claimed from this run. The current workflow adds protected-core,
protected-worker and protected-pair evidence to both Linux and native Windows
uploads. This is a real evidence gap, not a reason to rerun the unchanged revision.
All available original artifacts remain retained; no cancellation, timeout change
or failure waiver was used. The [CI index](../tests/results/M2/2026-10-15-protected-comp-pair/original-ci-index.json)
identifies all six original ZIPs. Its portable review archive includes full logs,
APIs, audits and all PNGs: 122 files, 3,672,797 bytes, SHA-256
`147caaed047aca6e896ac168ef0c7de461a9f69f5f33226dd15d33cd6515c637`.

## Remaining work

Next qualify protected paired assembly/publication against real nominal producer
provenance, including both-processed, mixed uniform/protected and existing-media
pairs. Then wire the explicit policy into desktop preparation with Review/Apply,
Undo/Redo, save/reopen, repeated history and shared live/offline output acceptance.
Do not merely relax the current controller refusal. Fractional/post-SRC timing,
protected history, crashes during rendering and publication, independent quality,
native Windows execution of the new worker/pair, installed delivery and all-Europe
remain required. M2–M11/F/Q/C/N/X004/X005 are not complete. No installed app, audio
route, VM or equalizer was changed.
