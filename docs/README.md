# Documentation index

These documents contain the frozen scope, implementation contracts, dated evidence and open gates. Later checkpoints supplement earlier evidence; they do not erase retained failures.

- [Product scope and frozen reference baseline](00-scope-baseline.md)
- [Traceable parity matrix and acceptance register](01-parity-matrix.md)
- [Architecture, threading and data flow](02-architecture.md)
- [Dependency, license and maintenance inventory](03-dependencies.md)
- [Staged backlog, dependencies and exit criteria](04-roadmap.md)
- [SLICE-001: record one track → in-process EQ → save/reopen → WAV](05-first-slice.md)
- [Existing equalizer work: read-only reuse audit](06-reuse-audit.md)
- [Bounded feasibility evidence](07-feasibility.md)
- [Windows and localization across Europe](08-platforms-and-localization.md)
- [X007: easy installation on supported Linux and Windows systems](88-easy-installation.md)
- [Desktop contextual localization foundation](87-desktop-localization.md)
- [Useful workflow previews and first package gates](89-workflow-previews.md)
- [Implemented session-state contract (S1/S2 foundation)](09-session-state-contract.md)
- [X004: import other suites' work files](10-project-import.md)
- [S3 prepared EQ and real-time transport contracts](11-engine-contract.md)
- [Active full-suite goal: progress and completion evidence](12-goal-progress.md)
- [S4 capture, disk writing and recording recovery](13-recording-contract.md)
- [S5 shared audio bridge and Linux PipeWire foundation](14-native-audio-contract.md)
- [S6a bounded read-ahead and take playback](15-playback-contract.md)
- [S6b immediate control ingress](16-immediate-controls.md)
- [X005 equipment profiling and profile editor](17-equipment-profiles.md)
- [S6c asynchronous project controller and desktop editor](18-desktop-controller.md)
- [S6d native playback ownership and clock bridge](19-native-playback-owner.md)
- [S6e desktop playback, output selection and live EQ](20-desktop-playback.md)
- [S6f native recording ownership and verified take attachment](21-native-recording-owner.md)
- [S6g desktop recording and recovery handoff](22-desktop-recording.md)
- [S7a: shared-engine offline float WAV export](23-offline-export.md)
- [S7b: desktop snapshot export](24-desktop-export.md)
- [Equalizer reuse update review](25-equalizer-reuse-updates.md)
- [S8a: portable per-channel routing intent](26-project-routing.md)
- [Saved recording-monitor preferences (S8b)](27-monitoring-preferences.md)
- [Recording-job discovery and recovery (S8c)](28-recording-discovery.md)
- [M2a: transactional track and clip editing](29-multitrack-edits.md)
- [M2b: desktop timeline and selected-track workflow](30-desktop-timeline.md)
- [M2c1: shared-clock multitrack EQ and mix graph](31-multitrack-mix.md)
- [M2c2: native shared-clock mix playback and desktop selection](32-native-mix-playback.md)
- [M2c3: saved master layout, matrix and output intent](33-master-matrix.md)
- [M2c4: retained callback faults and bounded timing evidence](34-native-timing.md)
- [M2d1: one admitted clock for playback and armed raw capture](35-shared-playback-capture.md)
- [M2d2: production ownership for simultaneous playback and recording](36-duplex-recording-owner.md)
- [M2d3: desktop armed recording and grouped take admission](37-desktop-duplex-recording.md)
- [M2d4a: paced 32-track recording duration and recovery](38-recording-duration.md)
- [M2d4b1: native duration qualifier and route admission](39-native-duration-qualification.md)
- [M2d4c1: bounded recording-writer diagnostics](40-writer-backlog-diagnostics.md)
- [M2d4c2: admitted recording reserve and checkpoint phases](41-checkpoint-burst-policy.md)
- [M2d4c3: distinguish callback wall time from thread CPU time](42-native-callback-cpu-diagnostics.md)
- [Recording checkpoints with one checked audio flush](43-recording-checked-flush.md)
- [Native callback resource and cycle context](44-native-thread-resource-diagnostics.md)
- [Avoid redundant smoothing work in settled EQ](45-steady-eq-ramp-work.md)
- [Native processing stage observations](46-native-processing-stage-diagnostics.md)
- [Bounded European language inventory audit](47-europe-language-inventory-audit.md)
- [Individual processing calls in retained native callbacks](48-individual-native-processing-calls.md)
- [Windows development without a purchased signing certificate](49-windows-signing-budget.md)
- [Close requests and historical controller errors](50-desktop-close-error-baseline.md)
- [Prepared punch capture with uninterrupted playback](51-punch-capture-foundation.md)
- [Select a published track before the next UI tick](52-published-track-selection.md)
- [Latency-aware prepared punch locators](53-latency-aware-punch-locators.md)
- [Project-backed punch recording controls](54-project-punch-controls.md)
- [Owned native punch qualification](55-native-punch-qualification.md)
- [Native desktop punch checkpoint](56-native-desktop-punch.md)
- [Public source backup and Windows copy](57-repository-backup.md)
- [Main branch protection](58-repository-branch-protection.md)
- [Native punch interruption and recovery checkpoint](59-native-punch-fault-recovery.md)

[Architecture decisions](decisions/) · [Acceptance receipts](../tests/results/) · [Active goal](../GOAL.md)

- [Original clip gain, editable fades and bounded resampler feasibility](129-clip-gain-and-fades.md)
- [Prepared resampling, exact source/project coordinates and integration gates](130-prepared-resampling.md)

- [Saved per-track input latency controls](60-input-latency-controls.md)

- [Recording-only Auto monitoring](61-auto-recording-monitoring.md)
- [Deferred capture start for manual punch](62-deferred-capture-start.md)

- [Manual punch engine owner and take lifetime](63-manual-punch-engine-owner.md)

- [Manual recording disk/group ownership](64-manual-recording-control-owner.md)

- [Native manual recording and late service](65-native-manual-recording.md)
- [Repeatable default WAV/RF64 exports](66-repeatable-export.md)

- [X006: scalable track counts for recording studios](67-track-scalability.md)
- [Native buffer readiness, extent and ownership](73-native-buffer-acquisition.md)
- [Canonical recording monitoring feedback and bounded repeated takes](74-recording-monitor-feedback.md)
- [Manual recording desktop worker and cancellation delivery](75-manual-desktop-worker.md)

- [Manual recording desktop controls and preview lifetime](76-manual-recording-panel.md)

- [Native manual fault recovery and unresolved timing/alignment](68-native-manual-fault-recovery.md)

- [Native manual channel markers and processing-stage diagnostics](69-native-manual-port-tracing.md)

- [Priority Stop/Cancel independent of manual disk startup](70-manual-priority-interruption.md)

- [Public native buffer handoff observations](71-native-port-handoff.md)

- [Controlled native allocation before publication readiness](72-controlled-native-startup.md)

- [Native manual desktop evidence and neutral buffers](77-native-manual-panel.md)

- [Resource-admitted large projects: first implementation](78-resource-admitted-projects.md)

- [Shared media handles and decoded-page cache](79-shared-media-cache.md)

- [Model-backed session lists and viewport timeline](80-virtualized-session-views.md)

- [Configurable Undo resource settings](81-history-resource-admission.md)

- [Retained immutable session resources](82-retained-session-resources.md)

- [Native Windows capture foundation and honest waveform acceptance](96-windows-capture-foundation.md)

- [Native Windows playback toward a useful preview](97-windows-native-playback.md)
- [Windows desktop foundation and independent storage faults](98-windows-desktop-foundation.md)

- [Native Windows single-track desktop workflow and original sample refusals](99-windows-desktop-workflow.md)
- [Reviewed localization and equipment-editor refresh](100-localization-reuse-refresh.md)
- [Windows installer and source-pair preparation](101-windows-installer-preparation.md)
- [Windows clean-installed workflow preview](102-windows-installed-workflow-preview.md)

- [Desktop foreign-project inspection preview and retirement](115-desktop-import-inspection.md)

- [Portable saved inspection bundles](116-inspection-bundles.md)

- [Bounded synthetic recording CI writer observations](117-recording-ci-observations.md)

- [Frozen original native-writer import corpus](118-native-writer-import-corpus.md)

- [Owned import intermediate properties and loss evidence](119-import-intermediate-properties.md)

- [Source-property inspection preview and independent validation](120-import-property-preview.md)

- [Native Windows desktop import/localization gate](121-windows-import-ui-gate.md)

- [Approved media folder/file handles](122-approved-media-roots.md)

- [Verified owned media staging foundation](125-verified-media-staging.md)

- [Owned media provenance, publication and interruption recovery](126-media-provenance-transaction.md)

- [127: desktop checked-media copying and explicit recovery](127-desktop-checked-media-copy.md)
- [ADR093: frozen checked copy child outcomes](decisions/093-checked-copy-child-outcomes.md)

- [128 — portable original import state and verified media provenance](128-portable-import-project-evidence.md)
- [ADR094 — owned original/loss evidence alongside destination state](decisions/094-portable-import-original-evidence.md)

- [Installed Linux pitch/stretch preview](138-installed-stretch-preview.md)

- [Explicit stretch processing regions and nominal visible crops](144-explicit-stretch-context.md) · [ADR103](decisions/103-explicit-stretch-processing-region.md)
- [Retained context, local timing and grouped-channel quality observations](145-stretch-region-quality.md) · [ADR104](decisions/104-local-warp-and-group-quality.md)
- [Exact normalized warp-map planner and multiple-anchor experiment](146-normalized-warp-map-planner.md) · [ADR105](decisions/105-normalized-warp-map-planner.md)

- [Anchored alternative scheduler and measured acoustic limits](147-anchored-stretch-candidate.md) · [ADR106](decisions/106-transient-protected-warp-next.md)

- [Original transient-protected experiment](148-transient-protected-warp.md) · [ADR107](decisions/107-protected-warp-integration-scope.md)
- [Editable protected stretch and uncommitted audition](149-editable-protected-warp.md) · [ADR108](decisions/108-editable-protected-warp.md)

- [Protected preview receipts and current inputs](150-protected-preview-gates.md) · [ADR109](decisions/109-protected-preview-gates.md)

- [Installed protected stretch preview observations](151-installed-protected-preview.md)
- [Bounded render-worker acceptance observation](152-bounded-worker-observation.md)
- [Native and installed protected Windows preview](153-native-installed-protected-preview.md)
- [Retained render review after restarting](154-render-restart-review.md)
