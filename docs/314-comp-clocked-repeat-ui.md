# Repeated clocked comp editing in the desktop

Date: 2026-10-15. Source: `a0ff84d`.

## Workflow qualified

The existing clocked protected pair path now has actual main-window acceptance
coverage across repeated edits. A protected/protected bank and a protected/uniform
bank each start from a real published assembly and perform three generations:

1. Seed fractional source origins at the current media edges, then change both
   clips from normal speed to 1.5 through the actual **Apply speed** control.
2. Crop the newly assembled audio again with fractional timing and an exact 2/3
   rate. The speed field displays 0.667; clicking Apply without changing its value
   must preserve the original rational rate. The new render retains 1.5 baked pitch
   and both original semantic anchors outside the active crop.
3. Keep one assembled take with existing handles and regenerate only the incoming
   original side. Source ancestry and playback state survive the mixed edit.

Each generation uses the real timeline **Save and render** dialog, waits for owned
workers, opens the current saved transition for review, and explicitly applies it.
The tests verify no audio changes before Apply, fresh operation identities, durable
references, current review status, Undo/Redo, save/reopen and exported audio.
Both first generations require the post-SRC project-domain assembly path.

Worker PCM is compared sample by sample with recomputation from original geometry.
The shared live-render path is compared with WAV export, unchanged audio outside
the edit is checked, and synthetic floating-point peaks above unity verify headroom.
These synthetic checks do not play through the user's speakers.

The protected bank also exercises cancellation during preparation, a concurrent
source edit, corrupt history before rendering, corrupt ancestry after Review, and
closing a pending Apply. Audio must remain unchanged and children must retire.
Actual project reservations must return to zero after the window is destroyed.
Stage observations retain the existing 30-second deadline; no worker or legacy
suite deadline was relaxed. The mixed bank avoids repeating the same failure bank.

## Qualification evidence

Four Release suites pass in 377.21 seconds, including the older
uniform and protected repeated desktop regressions. The two new Debug suites pass
in 287.33 seconds. Source and executable snapshots remain unchanged
through each run.

Per configuration, the new banks complete six adoptions with ten successful
adoption workers, two retained mixed cases and five failure/close cases. They check
665,448 candidate PCM samples, 280,132 samples outside the edited region and
295,920 live/export samples. All six new exports are byte-identical across builds;
nine legacy protected exports retain their previous hashes.

All eleven stage observations per build pass the unchanged 30-second bound; maxima
are 8,693 ms (Release) and 21,757 ms (Debug). Twenty-five screenshots per
configuration are retained. The second-generation protected create/review dialogs
were visually inspected; save/render and Apply captions and fields are visible.

[Portable local evidence](../tests/results/M2/2026-10-15-comp-clocked-repeat-ui/index.json)
retains exact commands, source/executable hashes, original PCM, projects, process
outcomes and independent file/peak/comparison audits.

## Original CI failures and corrections

[Original run 38717457857](https://github.com/rhamenator/soundcurrent-daw/actions/runs/38717457857)
at `f642367` is retained as **failed**. All ten full job logs, nine original ZIPs and
220 PNGs were preserved before advancing the draft PR. The portable
[original-CI review](../tests/results/M2/2026-10-15-comp-clocked-repeat-ui/original-ci-index.json)
includes the failure evidence and detailed native audits.

### Windows crash-observation paths

The native comp bank passed 38 of 40 suites. The clocked protected history and
protected-pair suites failed inside their shared setup's abrupt-publication check.
The original ZIP contains the stage-one files that the assertion reported absent:
their unprefixed Windows paths are 261 and 262 characters long. No completion marker
was expected at that crash boundary. The inspection fixture used an unprefixed
`std::filesystem::exists` call while publication used the existing long-path helper.

Test-only correction `5964bdc` uses `fileSystemApiPath` for positive and negative
crash/recovery existence observations and adds an explicit positive existence
check to the native long-path fixture. Three focused Release suites pass in
50.65 seconds and three Debug suites in 141.09 seconds. Both changed
translation units pass MinGW syntax checks. Native execution of the correction is
pending; Linux success and syntax checks do not prove it.

The original native direct-clocked worker did pass six successes and 78 refusals;
the mixed pair bank passed three adoptions and five workers. Two of its three
exports match Linux exactly; the retained-take export differs by at most
`1.1920928955078125e-7`. The history and protected-pair banks did not reach their
intended workflows. The later 93-test native bank was not run after the first
bank failed. Desktop banks of 22/6/1 suites and import/localization passed;
these results do not turn the original run green.

### Linux whole-job budget

All 216 CTest suites passed in disjoint 72-test shards. Shard zero took 936.23 seconds
of test time after a 637-second build, then completed its retained-evidence checks
and uploaded its artifact. GitHub nevertheless marked the whole job cancelled at
the 30-minute limit, with an explicit timeout annotation. The Linux aggregate failed.

CI now assigns all 218 tests once across five shards using 216 original measured
timings and two documented estimates. Estimated test time is about 576.48 seconds
per shard, leaving room for compilation, evidence verification and uploads. The
30-minute job limit, individual test deadlines, every archive gate and the aggregate
requirement remain intact. Fresh CI must confirm the revised schedule; this is not
a waiver of the original failure. See [ADR 201](decisions/201-ci-whole-job-budget.md).


## Scope and remaining work

This checkpoint adds workflow acceptance and CI coverage for the application
integration delivered in checkpoint 313; it changes no production DSP or UI text.
The decimal speed field still supports decimal user entry; this test proves that
an unchanged rounded display preserves an already-stored exact rational value.
It does not add arbitrary rational text entry.

New coverage is synthetic stereo at 48 kHz. Broader material/layout/rate coverage,
short uniform analysis windows, independent processing quality, installed native
workflows and all-Europe localization remain open. Whole-suite parity is not
established by these workflows.

## Next implementation task

Retain and audit native worker, pair and desktop CI. Deliver a usable preview only
after its exact source, packaging and native workflow gates pass. Keep the measured
short uniform-window refusal visible and implement a safe supported strategy for
that case without silently changing protection or timing semantics.
